#include "rawframe/render_scene/scene.h"

#include "bounded.h"
#include "camera.h"
#include "cascades.h"
#include "decals.h"
#include "lights.h"
#include "lines.h"
#include "probes.h"
#include "rawframe/material/material.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"
#include "scene_state.h"
#include "skinning.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>

namespace rawframe::render_scene {

using particles::Beam;
using particles::ParticleEmitter;
using particles::Trail;

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderSceneError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderSceneDomain, code(error), why).error()};
}

/// A clear sky's color, sRGB: the sky's default (D288).
constexpr std::uint32_t kClearSky = 0x8FB8EBFF;
/// The default ground's albedo, sRGB: a fifth, near the Earth's land's
/// (D304).
constexpr std::uint32_t kGround = 0x7C7C7CFF;
/// The angle the Sun's disc spans seen from the Earth, in radians, the
/// default sun's (D347); and the most a sun's may, twenty degrees.
constexpr float kSunAngle = 0.0093F;
constexpr float kMostSunAngle = 0.35F;

/// Whether an instance's values are all ones it can be drawn with.
bool wellFormed(const ModelInstance& instance) noexcept {
    const Model& kModel = instance.model;
    return std::isfinite(kModel.scaleX) && std::isfinite(kModel.scaleY) && std::isfinite(kModel.scaleZ) &&
           std::ranges::all_of(instance.position,
                               [](double value) {
                                   return std::isfinite(value);
                               }) &&
           std::ranges::all_of(instance.rotation, [](float value) {
               return std::isfinite(value);
           });
}

} // namespace

void Scene::State::postProcessesOf(const SceneCamera& camera) {
    frame.postProcesses.clear();
    frame.postProcessesLeftOut = 0;
    for (const PostProcess& kAsked : camera.postProcesses) {
        // Material nought, or a weight of nought or less, runs nothing.
        if (std::isfinite(kAsked.weight) && (kAsked.material == 0 || kAsked.weight <= 0)) {
            continue;
        }
        const auto kMaterial =
            std::ranges::find(settings.postProcesses, kAsked.material, &ScenePostProcessMaterial::id);
        if (!std::isfinite(kAsked.weight) || kMaterial == settings.postProcesses.end()) {
            ++frame.postProcessesLeftOut;
            continue;
        }
        if (frame.postProcesses.size() == settings.limits.maximumPostProcesses) {
            ++frame.postProcessesLeftOut;
            continue;
        }
        frame.postProcesses.push_back(ScenePostProcess{.insertion = kMaterial->insertion,
                                                       .blob = kMaterial->blob,
                                                       .texture = kMaterial->texture,
                                                       .weight = std::min(kAsked.weight, 1.0F)});
    }
}

void Scene::State::lights() {
    const Sun kSun = sunNow.value_or(Sun{.directionX = -0.3F,
                                         .directionY = -1.0F,
                                         .directionZ = -0.4F,
                                         .illuminance = 100000,
                                         .color = 0xFFFFFFFF,
                                         .angle = kSunAngle});
    // A clear day's sky: its light, and its blue.
    const Sky kSky = skyNow.value_or(Sky{.luminance = 5000, .color = kClearSky, .ground = kGround});
    const Vector kToSun = normalized({-kSun.directionX, -kSun.directionY, -kSun.directionZ});
    const Vector kSunColor = colorOf(kSun.color);
    const Vector kSkyColor = colorOf(kSky.color);
    const float kIlluminance = std::isfinite(kSun.illuminance) ? std::max(kSun.illuminance, 0.0F) : 0.0F;
    const float kLuminance = std::isfinite(kSky.luminance) ? std::max(kSky.luminance, 0.0F) : 0.0F;
    frame.lights = SceneLights{
        .toSun = kToSun,
        .sun = {kSunColor[0] * kIlluminance, kSunColor[1] * kIlluminance, kSunColor[2] * kIlluminance},
        .sky = {kSkyColor[0] * kLuminance, kSkyColor[1] * kLuminance, kSkyColor[2] * kLuminance},
        .sunWidth = 2 * std::tan(std::clamp(std::isfinite(kSun.angle) ? kSun.angle : 0.0F, 0.0F, kMostSunAngle) / 2),
        .environment = kSky.environment};
    // The ground, level and unshadowed: what reaches it, the sun's at
    // its height and the whole upper sky's (π times its luminance),
    // given back evenly by its albedo.
    const Vector kAlbedo = colorOf(kSky.ground);
    const float kSunOnGround = std::max(kToSun[1], 0.0F) / std::numbers::pi_v<float>;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        frame.lights.ground[channel] =
            kAlbedo[channel] * ((frame.lights.sun[channel] * kSunOnGround) + frame.lights.sky[channel]);
    }
}

const SceneFrame& Scene::State::queue(const SceneCamera& camera) {
    frame.eye = camera.eye;
    frame.time = {seconds, queuedSeconds.value_or(seconds)};
    queuedSeconds = seconds;
    frame.draws.clear();
    frame.palette.clear();
    frame.skinned = 0;
    frame.paletteOverLimit = 0;
    frame.drawn = 0;
    frame.placed = 0;
    frame.tracked = 0;
    frame.culled = 0;
    frame.hidden = 0;
    frame.malformed = 0;
    frame.unknownMeshes = 0;
    frame.unknownMaterials = 0;
    frame.overLimit = 0;
    frame.shadows.count = 0;
    frame.shadows.filter = settings.shadows.filter;
    frame.shadows.casters.clear();
    frame.shadows.overLimit = 0;
    frame.lights3d.clear();
    frame.lightsCulled = 0;
    frame.lightsOverLimit = 0;
    frame.clusterOverflow = 0;
    lights();
    candidates.clear();
    sunCandidates.clear();
    const bool kLightShadows =
        settings.lightShadows.side > 0 && std::ranges::any_of(punctual, [](const LightInstance& light) {
            return light.light.shadows && light.light.lumens > 0;
        });
    order.clear();
    for (const ModelInstance& instance : extracted) {
        order.push_back(&instance);
    }
    std::ranges::sort(order, [](const ModelInstance* left, const ModelInstance* right) {
        return std::tuple{left->model.mesh, left->entity, left->component} <
               std::tuple{right->model.mesh, right->entity, right->component};
    });
    // A camera that sees nothing culls everything.
    const bool kSees = std::ranges::all_of(camera.eye,
                                           [](double value) {
                                               return std::isfinite(value);
                                           }) &&
                       std::isfinite(camera.yaw) && std::isfinite(camera.pitch) && std::isfinite(camera.fovY) &&
                       std::isfinite(camera.near) && std::isfinite(camera.aspect) && camera.fovY > 0 &&
                       camera.fovY < std::numbers::pi_v<float> && camera.near > 0 && camera.aspect > 0 &&
                       orientationSound(camera);
    const auto [kRight, kUp, kForward] = kSees ? axesOf(camera) : axesOf(0, 0);
    const float kHalf = kSees ? camera.fovY / 2 : 0.5F;
    const float kAspect = kSees ? camera.aspect : 1.0F;
    const float kNear = kSees ? camera.near : 0.1F;
    const CameraMatrices kMatrices = matricesOf(SceneCamera{.yaw = kSees ? camera.yaw : 0,
                                                            .pitch = kSees ? camera.pitch : 0,
                                                            .fovY = kHalf * 2,
                                                            .near = kNear,
                                                            .aspect = kAspect,
                                                            .orientation = kSees ? camera.orientation : std::nullopt});
    frame.view = kMatrices.view;
    frame.projection = kMatrices.projection;
    frame.exposure = std::isfinite(camera.exposure) ? camera.exposure : 15.0F;
    frame.metering = meteringOf(camera);
    frame.grading = gradingOf(camera.grading);
    postProcessesOf(camera);
    frame.occlusion = occlusionOf(camera.occlusion);
    frame.bloom = bloomOf(camera.bloom);
    frame.reflections = reflectionsOf(camera.reflections);
    frame.motionBlur = motionBlurOf(camera.motionBlur);
    frame.depthOfField = depthOfFieldOf(camera.depthOfField);
    frame.contactShadows = contactShadowsOf(camera.contactShadows);
    frame.tonemapper = camera.tonemapper <= static_cast<std::uint32_t>(Tonemapper::Linear)
                           ? static_cast<Tonemapper>(camera.tonemapper)
                           : Tonemapper::Agx;
    frame.forward = kForward;
    const bool kShadows = kSees && settings.shadows.cascades > 0 && settings.shadows.side > 0 &&
                          settings.shadows.distance > kNear &&
                          (frame.lights.sun[0] > 0 || frame.lights.sun[1] > 0 || frame.lights.sun[2] > 0);
    if (kShadows) {
        fitCascades(settings.shadows,
                    frame.lights.toSun,
                    camera,
                    {kRight, kUp, kForward},
                    kHalf,
                    kAspect,
                    kNear,
                    frame.shadows);
    }
    // The view's side planes in the eye's axes, each normal pointing
    // out: what lies past one by more than its radius is out of view.
    const float kWide = std::atan(std::tan(kHalf) * kAspect);
    const std::array<Vector, 4> kPlanes = {{{0, std::cos(kHalf), std::sin(kHalf)},
                                            {0, -std::cos(kHalf), std::sin(kHalf)},
                                            {std::cos(kWide), 0, std::sin(kWide)},
                                            {-std::cos(kWide), 0, std::sin(kWide)}}};
    bool full = false;
    for (const ModelInstance* instance : order) {
        const Model& kModel = instance->model;
        if (!wellFormed(*instance)) {
            ++frame.malformed;
            continue;
        }
        if (kModel.mesh == 0 || (kModel.color & 0xFFU) == 0 || kModel.scaleX == 0 || kModel.scaleY == 0 ||
            kModel.scaleZ == 0) {
            ++frame.hidden;
            continue;
        }
        const auto kMesh = meshes.find(kModel.mesh);
        if (kMesh == meshes.end()) {
            ++frame.unknownMeshes;
            continue;
        }
        // Relative to the eye in doubles, then floats: far from the
        // origin, a float would lose the model's place before it is small.
        const Vector kPlace = {static_cast<float>(instance->position[0] - camera.eye[0]),
                               static_cast<float>(instance->position[1] - camera.eye[1]),
                               static_cast<float>(instance->position[2] - camera.eye[2])};
        const bool kTurned = std::ranges::any_of(instance->rotation, [](float value) {
            return value != 0;
        });
        const std::array<Vector, 3> kTurn = turnOf(kTurned ? instance->rotation : std::array<float, 4>{0, 0, 0, 1});
        const Vector kScale = {kModel.scaleX, kModel.scaleY, kModel.scaleZ};
        const Bounded& kBounds = kMesh->second;
        Vector center = kPlace;
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                center[row] += kTurn[column][row] * kScale[column] * kBounds.center[column];
            }
        }
        // A posed mesh may reach past its bind's bounds: twice as far
        // holds a limb swung about its middle (D508).
        const float kRadius = kBounds.radius * (instance->joints != 0 ? 2.0F : 1.0F) *
                              std::max({std::abs(kScale[0]), std::abs(kScale[1]), std::abs(kScale[2])});
        SceneDraw draw{.mesh = kModel.mesh, .entity = instance->entity};
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                draw.model[(column * 4) + row] = kTurn[column][row] * kScale[column];
                draw.normal[(column * 4) + row] = kTurn[column][row] / kScale[column];
            }
        }
        draw.model[12] = kPlace[0];
        draw.model[13] = kPlace[1];
        draw.model[14] = kPlace[2];
        draw.model[15] = 1;
        // Where it was: the frame before's turn and place, relative to
        // this frame's eye.
        const std::pair kKey{instance->entity, instance->component};
        draw.previous = draw.model;
        const auto kBefore = placed.find(kKey);
        if (kBefore != placed.end()) {
            for (std::size_t column = 0; column < 3; ++column) {
                for (std::size_t row = 0; row < 3; ++row) {
                    draw.previous[(column * 4) + row] = kBefore->second.turned[(column * 3) + row];
                }
                draw.previous[12 + column] = static_cast<float>(kBefore->second.position[column] - camera.eye[column]);
            }
        }
        const Vector kColor = colorOf(kModel.color);
        draw.color = {kColor[0], kColor[1], kColor[2], static_cast<float>(kModel.color & 0xFFU) / 255.0F};
        // A draw for each run of parts of one material: the Model's for
        // all when it names one, else each part's own (D314); runs that
        // come to one material are one draw.
        const auto kPlaceOf = [&](std::uint64_t id) -> std::uint32_t {
            if (id == 0) {
                return 0;
            }
            const auto kMaterial = materials.find(id);
            if (kMaterial == materials.end()) {
                ++frame.unknownMaterials;
                return 0;
            }
            return kMaterial->second;
        };
        // Its palette, the frame's from here, for its draws and its
        // shadows' alike, and the frame before's (D510); past the
        // frame's room, drawn as bound (D508).
        if (instance->joints != 0 &&
            !paletteInto(std::span{posed}.subspan(instance->palette, instance->joints),
                         kBefore != placed.end() ? std::span<const Matrix>{kBefore->second.palette}
                                                 : std::span<const Matrix>{},
                         settings.limits.maximumPalette,
                         frame.palette,
                         draw)) {
            ++frame.paletteOverLimit;
        }
        std::vector<SceneDraw> runs;
        const std::uint32_t kOwn = kPlaceOf(kModel.material);
        for (const Run& kRun : kBounds.runs) {
            const std::uint32_t kAt = kModel.material != 0 ? kOwn : kPlaceOf(kRun.material);
            if (!runs.empty() && runs.back().material == kAt) {
                runs.back().indexCount += kRun.indexCount;
                continue;
            }
            draw.firstIndex = kRun.firstIndex;
            draw.indexCount = kRun.indexCount;
            draw.material = kAt;
            runs.push_back(draw);
        }
        // A model near enough casts into the shadows, seen or not: a
        // caster behind the eye still shades what is before it.
        const float kAway = std::sqrt((center[0] * center[0]) + (center[1] * center[1]) + (center[2] * center[2]));
        for (const SceneDraw& kRun : runs) {
            if (kLightShadows) {
                candidates.push_back({.draw = kRun, .center = center, .radius = kRadius});
            }
            if (frame.shadows.count > 0 && kAway - kRadius <= frame.shadows.distance) {
                sunCandidates.push_back({.draw = kRun, .center = center, .radius = kRadius});
            }
        }
        const Vector kSeen = {(kRight[0] * center[0]) + (kRight[1] * center[1]) + (kRight[2] * center[2]),
                              (kUp[0] * center[0]) + (kUp[1] * center[1]) + (kUp[2] * center[2]),
                              -((kForward[0] * center[0]) + (kForward[1] * center[1]) + (kForward[2] * center[2]))};
        bool outside = !kSees || kSeen[2] - kRadius > -kNear;
        for (const Vector& kPlane : kPlanes) {
            outside = outside || (kPlane[0] * kSeen[0]) + (kPlane[1] * kSeen[1]) + (kPlane[2] * kSeen[2]) > kRadius;
        }
        if (outside) {
            ++frame.culled;
            continue;
        }
        // Past the limit, this model and every one after it are left
        // out, so what is drawn is a prefix of the order.
        full = full || frame.drawn == settings.limits.maximumModels;
        if (full) {
            ++frame.overLimit;
            continue;
        }
        Placement& now = placing[kKey];
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                now.turned[(column * 3) + row] = draw.model[(column * 4) + row];
            }
        }
        now.position = instance->position;
        now.palette.assign(frame.palette.begin() + draw.palette, frame.palette.begin() + draw.palette + draw.joints);
        frame.skinned += draw.joints != 0 ? 1 : 0;
        frame.draws.insert(frame.draws.end(), runs.begin(), runs.end());
        ++frame.drawn;
        frame.placed += instance->placed ? 1 : 0;
        frame.tracked += instance->tracked ? 1 : 0;
    }
    // The translucent after the opaque, farthest first, so each blends
    // over what is behind it (D305); the opaque keep their order.
    const auto kTranslucent = std::ranges::stable_partition(frame.draws, [this](const SceneDraw& draw) {
        return draw.material >= translucent.size() || !translucent[draw.material];
    });
    frame.translucent = kTranslucent.size();
    const auto kAway = [](const SceneDraw& draw) {
        return (draw.model[12] * draw.model[12]) + (draw.model[13] * draw.model[13]) +
               (draw.model[14] * draw.model[14]);
    };
    std::ranges::stable_sort(kTranslucent, [&kAway](const SceneDraw& left, const SceneDraw& right) {
        return kAway(left) > kAway(right);
    });
    // The opaque by their material's program, so a device sets each
    // once (D485), then by its texture, so it binds each once (D309);
    // within one, by mesh as they were.
    const auto kTextureOf = [this](const SceneDraw& draw) {
        return draw.material < frame.textures.size() ? frame.textures[draw.material] : SceneTextures{};
    };
    // A program is its material's alone, so it is known by the
    // material's place; the engine's own program, nought, first.
    const auto kProgramOf = [this](const SceneDraw& draw) {
        return draw.material < frame.programs.size() && frame.programs[draw.material] != nullptr ? draw.material : 0U;
    };
    std::ranges::stable_sort(std::ranges::subrange(frame.draws.begin(), kTranslucent.begin()),
                             [&kTextureOf, &kProgramOf](const SceneDraw& left, const SceneDraw& right) {
                                 const std::uint32_t kLeftProgram = kProgramOf(left);
                                 const std::uint32_t kRightProgram = kProgramOf(right);
                                 return kLeftProgram != kRightProgram ? kLeftProgram < kRightProgram
                                                                      : kTextureOf(left) < kTextureOf(right);
                             });
    castIntoCascades();
    temporal(camera, kSees);
    frame.metering.snap = frame.metering.enabled && (!meteredBefore || !continuous);
    meteredBefore = frame.metering.enabled;
    // The lights and the decals in one clustered structure (D339).
    named.clear();
    clusterLights(frame,
                  punctual,
                  camera,
                  {kRight, kUp, kForward},
                  {.sees = kSees, .half = kHalf, .aspect = kAspect, .near = kNear},
                  settings.limits,
                  shadowed,
                  named);
    clusterDecals(frame,
                  decals,
                  camera,
                  {kRight, kUp, kForward},
                  {.sees = kSees, .half = kHalf, .aspect = kAspect, .near = kNear},
                  settings.limits,
                  named);
    clusterProbes(frame,
                  probes,
                  camera,
                  {kRight, kUp, kForward},
                  {.sees = kSees, .half = kHalf, .aspect = kAspect, .near = kNear},
                  settings.limits,
                  named);
    packClusters(frame, named, settings.limits);
    shadowLights(frame, shadowed, candidates, settings.lightShadows, settings.limits);
    // The particles, on a clock the Host's timeline moves (D352).
    const std::array<Vector, 3> kAxes = {kRight, kUp, kForward};
    const ViewShape kView{.sees = kSees, .half = kHalf, .aspect = kAspect, .near = kNear};
    particles.update(frame.particles,
                     emitters,
                     trails,
                     beams,
                     {.eye = camera.eye,
                      .sees =
                          [&kAxes, &kView](const Vector& center, float radius) {
                              return !outsideView(kAxes, kView, center, radius);
                          }},
                     materials,
                     settings.limits.particles,
                     camera.elapsed);
    // The lines shown, on the material after the frame's own (D464).
    addLines(frame, lines, camera.eye);
    return frame;
}

void Scene::State::castIntoCascades() {
    SceneShadows& shadows = frame.shadows;
    for (std::size_t at = 0; at < shadows.count; ++at) {
        ShadowCascade& cascade = shadows.cascades[at];
        const Matrix& kSeen = cascade.viewProjection;
        // How far a meter reaches along each of the cascade's axes.
        std::array<float, 3> reach{};
        for (std::size_t row = 0; row < 3; ++row) {
            reach[row] = std::sqrt((kSeen[row] * kSeen[row]) + (kSeen[4 + row] * kSeen[4 + row]) +
                                   (kSeen[8 + row] * kSeen[8 + row]));
        }
        cascade.firstCaster = static_cast<std::uint32_t>(shadows.casters.size());
        for (const ShadowCandidate& candidate : sunCandidates) {
            bool inside = true;
            for (std::size_t row = 0; row < 3; ++row) {
                const float kAt = (kSeen[row] * candidate.center[0]) + (kSeen[4 + row] * candidate.center[1]) +
                                  (kSeen[8 + row] * candidate.center[2]) + kSeen[12 + row];
                const float kMargin = candidate.radius * reach[row];
                inside = inside && kAt >= (row == 2 ? 0.0F : -1.0F) - kMargin && kAt <= 1 + kMargin;
            }
            if (!inside) {
                continue;
            }
            if (shadows.casters.size() < settings.limits.maximumModels) {
                shadows.casters.push_back(candidate.draw);
            } else {
                ++shadows.overLimit;
            }
        }
        cascade.casterCount = static_cast<std::uint32_t>(shadows.casters.size()) - cascade.firstCaster;
    }
}

void Scene::State::temporal(const SceneCamera& camera, bool sees) {
    std::swap(placed, placing);
    placing.clear();
    SceneTemporal& now = frame.temporal;
    now.enabled = settings.antiAliasing == AntiAliasing::Taa && sees;
    frame.fxaa = settings.antiAliasing == AntiAliasing::Fxaa && sees;
    frame.samples = settings.antiAliasing == AntiAliasing::Msaa && sees ? settings.multisamples : 1;
    now.jitter = now.enabled ? temporalJitter(frames) : std::array<float, 2>{0, 0};
    const Matrix kViewProjection = times(frame.projection, frame.view);
    std::array<double, 3> moved{};
    for (std::size_t axis = 0; axis < 3 && previousEye.has_value(); ++axis) {
        moved[axis] = camera.eye[axis] - (*previousEye)[axis];
    }
    continuous = previousEye.has_value() &&
                 (moved[0] * moved[0]) + (moved[1] * moved[1]) + (moved[2] * moved[2]) <= kCutDistance * kCutDistance;
    now.history = now.enabled && continuous;
    now.previousViewProjection = kViewProjection;
    if (now.history) {
        // The frame before's eye-relative places are this frame's moved
        // by how far the eye went.
        now.previousViewProjection = previousViewProjection;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                now.previousViewProjection[12 + row] +=
                    previousViewProjection[(axis * 4) + row] * static_cast<float>(moved[axis]);
            }
        }
    }
    previousEye = sees ? std::optional{camera.eye} : std::nullopt;
    previousViewProjection = kViewProjection;
    ++frames;
}

Scene::Scene(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Scene::~Scene() = default;

namespace {

/// The frame's materials: none's first, then the game's; a later line of
/// an identity replaces an earlier.
void placeMaterials(Scene::State& state, std::span<const SceneMaterial> materials) {
    state.materials.clear();
    state.frame.materials = {noMaterial()};
    state.frame.textures = {SceneTextures{}};
    state.frame.programs = {nullptr};
    state.translucent = {false};
    for (const SceneMaterial& kMaterial : materials) {
        if (kMaterial.id == 0) {
            continue;
        }
        const auto [kAt, kNew] =
            state.materials.try_emplace(kMaterial.id, static_cast<std::uint32_t>(state.frame.materials.size()));
        // A material that blends says so to the device too (D726).
        MaterialBlob blob = kMaterial.blob;
        if (kMaterial.translucent) {
            blob[kFlagsAt] = static_cast<float>(static_cast<std::uint32_t>(blob[kFlagsAt]) | kBlendsFlag);
        }
        if (kNew) {
            state.frame.materials.push_back(blob);
            state.frame.textures.push_back(kMaterial.textures);
            state.frame.programs.push_back(kMaterial.program);
            state.translucent.push_back(kMaterial.translucent);
        } else {
            state.frame.materials[kAt->second] = blob;
            state.frame.textures[kAt->second] = kMaterial.textures;
            state.frame.programs[kAt->second] = kMaterial.program;
            state.translucent[kAt->second] = kMaterial.translucent;
        }
    }
}

} // namespace

result::Result<std::unique_ptr<Scene>> Scene::create(const schema::SchemaRegistry& registry, SceneSettings settings) {
    auto state = std::make_unique<State>();
    const auto kQueryOf = [&registry](schema::ComponentTypeId id,
                                      std::size_t size) -> result::Result<world::ColumnQuery> {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(id));
        if (registry.descriptor(kComponent).size != size) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "a rawframe.model component is not that module's size");
        }
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kComponent, world::Access::Read}};
        return world::ColumnQuery::resolve(kTerms, registry);
    };
    for (const schema::ComponentTypeId kId : settings.models) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(Model)));
        state->models.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kId : settings.placements) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(kId));
        if (registry.descriptor(kComponent).size != sizeof(Placed)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "a rawframe.model component is not that module's size");
        }
        state->placements.push_back(kComponent);
    }
    for (const schema::ComponentTypeId kId : settings.tracking) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(kId));
        if (registry.descriptor(kComponent).size != sizeof(Tracked)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "a rawframe.model component is not that module's size");
        }
        state->tracking.push_back(kComponent);
    }
    if (settings.sun.has_value()) {
        RAWFRAME_TRY_ASSIGN(state->sun, kQueryOf(*settings.sun, sizeof(Sun)));
    }
    if (settings.sky.has_value()) {
        RAWFRAME_TRY_ASSIGN(state->sky, kQueryOf(*settings.sky, sizeof(Sky)));
    }
    for (const schema::ComponentTypeId kId : settings.points) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(PointLight)));
        state->lightQueries.emplace_back(std::move(query), false);
    }
    for (const schema::ComponentTypeId kId : settings.spots) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(SpotLight)));
        state->lightQueries.emplace_back(std::move(query), true);
    }
    for (const schema::ComponentTypeId kId : settings.probes) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(ReflectionProbe)));
        state->probeQueries.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kId : settings.decals) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(Decal)));
        state->decalQueries.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kId : settings.emitters) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(ParticleEmitter)));
        state->emitterQueries.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kId : settings.trails) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(Trail)));
        state->trailQueries.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kId : settings.beams) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(Beam)));
        state->beamQueries.push_back(std::move(query));
    }
    for (const std::uint64_t kId : {kBox, kSphere, kCylinder, kCapsule, kGrid}) {
        state->meshes.emplace(kId, bounded(engineMesh(kId)));
    }
    // A game's mesh of an engine mesh's identity is the game's.
    for (const SceneMesh& kMesh : settings.meshes) {
        if (kMesh.mesh != nullptr && !kMesh.mesh->positions.empty()) {
            state->meshes.insert_or_assign(kMesh.id, bounded(kMesh.mesh));
        }
    }
    placeMaterials(*state, settings.materials);
    state->settings = std::move(settings);
    if (const auto kPose = registry.find(physics3d::Pose3D::kComponentTypeId)) {
        state->pose = *kPose;
    }
    return std::unique_ptr<Scene>{new Scene{std::move(state)}};
}

void Scene::shownAt(double seconds) noexcept {
    state_->seconds = std::isfinite(seconds) ? seconds : 0;
}

const SceneFrame& Scene::queue(const SceneCamera& camera) {
    return state_->queue(camera);
}

void Scene::replaceMaterials(std::vector<SceneMaterial> materials,
                             std::vector<ScenePostProcessMaterial> postProcesses) {
    placeMaterials(*state_, materials);
    state_->settings.materials = std::move(materials);
    state_->settings.postProcesses = std::move(postProcesses);
}

void Scene::show(std::span<const SceneLine> lines) {
    state_->lines = soundLines(lines);
}

std::array<float, 2> temporalJitter(std::uint64_t frame) noexcept {
    const auto kRadical = [](std::uint64_t index, std::uint64_t base) {
        float fraction = 1;
        float result = 0;
        while (index > 0) {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    };
    const std::uint64_t kIndex = (frame % 8) + 1;
    return {kRadical(kIndex, 2) - 0.5F, kRadical(kIndex, 3) - 0.5F};
}

std::shared_ptr<const mesh::Mesh> Scene::mesh(std::uint64_t id) const {
    const auto kFound = state_->meshes.find(id);
    return kFound != state_->meshes.end() ? kFound->second.mesh : nullptr;
}

} // namespace rawframe::render_scene
