#include "rawframe/render_scene/scene.h"

#include "lights.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <numbers>
#include <tuple>

namespace rawframe::render_scene {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderSceneError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderSceneDomain, code(error), why).error()};
}

/// A clear sky's color, sRGB: the sky's default (D288).
constexpr std::uint32_t kClearSky = 0x8FB8EBFF;

/// A mesh with the sphere around it, in its own space.
struct Bounded {
    std::shared_ptr<const mesh::Mesh> mesh;
    Vector center{};
    float radius = 0;
};

Bounded bounded(std::shared_ptr<const mesh::Mesh> made) {
    Vector low = made->positions.front();
    Vector high = low;
    for (const mesh::Vector3& kPosition : made->positions) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], kPosition[axis]);
            high[axis] = std::max(high[axis], kPosition[axis]);
        }
    }
    const Vector kCenter = {(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2};
    float radius = 0;
    for (const mesh::Vector3& kPosition : made->positions) {
        const Vector kAway = {kPosition[0] - kCenter[0], kPosition[1] - kCenter[1], kPosition[2] - kCenter[2]};
        radius = std::max(radius, std::sqrt((kAway[0] * kAway[0]) + (kAway[1] * kAway[1]) + (kAway[2] * kAway[2])));
    }
    return Bounded{.mesh = std::move(made), .center = kCenter, .radius = radius};
}

/// The camera's metering made sound (D293): off when it asks for none, a
/// value is not finite, or its maximum is not above its minimum (a camera's
/// meter not yet set is all nought); the rates not negative,
/// the fractions within nought and one with low below high, and the
/// seconds since the frame before at most a quarter.
SceneMetering meteringOf(const SceneCamera& camera) noexcept {
    if (!camera.metering.has_value()) {
        return {};
    }
    AutoExposure asked = *camera.metering;
    if (!std::ranges::all_of(
            std::array{
                asked.minimum, asked.maximum, asked.brighten, asked.darken, asked.compensation, asked.low, asked.high},
            [](float value) {
                return std::isfinite(value);
            })) {
        return {};
    }
    if (asked.maximum <= asked.minimum) {
        return {};
    }
    asked.brighten = std::max(asked.brighten, 0.0F);
    asked.darken = std::max(asked.darken, 0.0F);
    asked.low = std::clamp(asked.low, 0.0F, 1.0F);
    asked.high = std::clamp(asked.high, asked.low, 1.0F);
    const float kElapsed = std::isfinite(camera.elapsed) ? std::clamp(camera.elapsed, 0.0F, 0.25F) : 0.0F;
    return {.enabled = true, .settings = asked, .elapsed = kElapsed};
}

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

/// `left` times `right`, column-major.
Matrix times(const Matrix& left, const Matrix& right) noexcept {
    Matrix out{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += left[(k * 4) + row] * right[(column * 4) + k];
            }
            out[(column * 4) + row] = sum;
        }
    }
    return out;
}

/// The eye's axes: right, up, and forward, from its yaw and pitch, pitch
/// kept short of straight up or down.
std::array<Vector, 3> axesOf(float yaw, float pitch) noexcept {
    constexpr float kSteepest = (std::numbers::pi_v<float> / 2) - 0.001F;
    const float kPitch = std::clamp(pitch, -kSteepest, kSteepest);
    const Vector kForward = {-std::sin(yaw) * std::cos(kPitch), std::sin(kPitch), -std::cos(yaw) * std::cos(kPitch)};
    const Vector kRight = normalized(cross(kForward, {0, 1, 0}));
    return {kRight, cross(kRight, kForward), kForward};
}

} // namespace

struct Scene::State {
    SceneSettings settings;
    std::vector<world::ColumnQuery> models;
    std::optional<world::ColumnQuery> sun;
    std::optional<world::ColumnQuery> sky;
    /// The point lights' queries, then the spot lights'.
    std::vector<std::pair<world::ColumnQuery, bool>> lightQueries;
    std::vector<LightInstance> punctual;
    std::optional<schema::ComponentRuntimeId> pose;
    std::map<std::uint64_t, Bounded> meshes;
    std::vector<ModelInstance> extracted;
    std::optional<Sun> sunNow;
    std::optional<Sky> skyNow;
    std::vector<const ModelInstance*> order;
    SceneFrame frame;
    /// What the frame before drew (D291): each model's turn and scale, and
    /// its place in the World, by entity and component; and this frame's,
    /// as it is queued.
    struct Placement {
        std::array<float, 9> turned{};
        std::array<double, 3> position{};
    };
    std::map<std::pair<world::EntityHandle, std::uint32_t>, Placement> placed;
    std::map<std::pair<world::EntityHandle, std::uint32_t>, Placement> placing;
    /// The eye the frame before was seen from, if it saw, and its view and
    /// projection, unjittered; the frames queued.
    /// Every model a punctual light's shadow may take, with its bounding
    /// sphere relative to the eye, in draw order; and whether each light
    /// kept for the view asks for shadows (D292).
    std::vector<ShadowCandidate> candidates;
    std::vector<bool> shadowed;
    /// The models near enough to cast into the sun's cascades (D298).
    std::vector<ShadowCandidate> sunCandidates;
    std::optional<std::array<double, 3>> previousEye;
    Matrix previousViewProjection{};
    std::uint64_t frames = 0;
    /// Whether the eye went on from the frame before, not first nor cut
    /// away; and whether that frame was metered.
    bool continuous = false;
    bool meteredBefore = false;

    void lights() {
        const Sun kSun = sunNow.value_or(Sun{
            .directionX = -0.3F, .directionY = -1.0F, .directionZ = -0.4F, .illuminance = 100000, .color = 0xFFFFFFFF});
        // A clear day's sky: its light, and its blue.
        const Sky kSky = skyNow.value_or(Sky{.luminance = 5000, .color = kClearSky});
        const Vector kToSun = normalized({-kSun.directionX, -kSun.directionY, -kSun.directionZ});
        const Vector kSunColor = colorOf(kSun.color);
        const Vector kSkyColor = colorOf(kSky.color);
        const float kIlluminance = std::isfinite(kSun.illuminance) ? std::max(kSun.illuminance, 0.0F) : 0.0F;
        const float kLuminance = std::isfinite(kSky.luminance) ? std::max(kSky.luminance, 0.0F) : 0.0F;
        frame.lights =
            SceneLights{.toSun = kToSun,
                        .sun = {kSunColor[0] * kIlluminance, kSunColor[1] * kIlluminance, kSunColor[2] * kIlluminance},
                        .sky = {kSkyColor[0] * kLuminance, kSkyColor[1] * kLuminance, kSkyColor[2] * kLuminance}};
    }

    /// The sun's cascades for this view (ADR-0051): the view from the near
    /// plane to the shadows' distance split between them, the logarithmic
    /// and uniform schemes blended; each a square in the sun's axes around
    /// its slice's bounding sphere, whose radius depends only on the lens so
    /// the square keeps its size as the eye turns, placed on whole texels
    /// of the World, not the eye, so it does not shimmer as the eye moves.
    /// Its depth reaches the shadows' distance toward the sun, for casters
    /// between the sun and the view.
    void cascades(const SceneCamera& camera, const std::array<Vector, 3>& axes, float half, float aspect, float near) {
        const ShadowSettings& kSettings = settings.shadows;
        SceneShadows& shadows = frame.shadows;
        shadows.count = std::min<std::size_t>(kSettings.cascades, shadows.cascades.size());
        shadows.side = kSettings.side;
        shadows.distance = kSettings.distance;
        const Vector& kToSun = frame.lights.toSun;
        // The sun's axes: across its square, then along its light.
        const Vector kAlong = {-kToSun[0], -kToSun[1], -kToSun[2]};
        const Vector kAcross =
            normalized(cross(kAlong, std::abs(kAlong[1]) < 0.99F ? Vector{0, 1, 0} : Vector{1, 0, 0}));
        const Vector kUpward = cross(kAcross, kAlong);
        const auto kDot = [](const Vector& left, const std::array<double, 3>& right) {
            return (left[0] * right[0]) + (left[1] * right[1]) + (left[2] * right[2]);
        };
        const auto kDotF = [](const Vector& left, const Vector& right) {
            return (left[0] * right[0]) + (left[1] * right[1]) + (left[2] * right[2]);
        };
        const Vector& kForward = axes[2];
        const float kTan = std::tan(half);
        float start = near;
        for (std::size_t at = 0; at < shadows.count; ++at) {
            const float kPart = static_cast<float>(at + 1) / static_cast<float>(shadows.count);
            const float kUniform = near + ((kSettings.distance - near) * kPart);
            const float kLogarithmic = near * std::pow(kSettings.distance / near, kPart);
            const float kEnd =
                (kSettings.logarithmicBlend * kLogarithmic) + ((1 - kSettings.logarithmicBlend) * kUniform);
            // The slice's bounding sphere: its center on the view's axis,
            // where it is nearest all eight corners.
            const float kNearHalf = start * kTan;
            const float kFarHalf = kEnd * kTan;
            const float kNearCorner = kNearHalf * kNearHalf * (1 + (aspect * aspect));
            const float kFarCorner = kFarHalf * kFarHalf * (1 + (aspect * aspect));
            const float kMiddle = std::clamp(
                ((kEnd * kEnd) - (start * start) + kFarCorner - kNearCorner) / (2 * (kEnd - start)), start, kEnd);
            const float kRadiusRaw = std::sqrt(std::max(((kEnd - kMiddle) * (kEnd - kMiddle)) + kFarCorner,
                                                        ((kMiddle - start) * (kMiddle - start)) + kNearCorner));
            // Rounded up to a sixteenth of a meter, so it is the same frame
            // to frame.
            const float kRadius = std::ceil(kRadiusRaw * 16) / 16;
            const float kTexel = 2 * kRadius / static_cast<float>(kSettings.side);
            const Vector kCenter = {kForward[0] * kMiddle, kForward[1] * kMiddle, kForward[2] * kMiddle};
            // Snapped on the World's texels: the eye's place in the sun's
            // axes in doubles, the center's offset from it in floats.
            const double kEyeAcross = kDot(kAcross, camera.eye);
            const double kEyeUpward = kDot(kUpward, camera.eye);
            const double kWorldAcross = std::floor((kEyeAcross + kDotF(kAcross, kCenter)) / kTexel) * kTexel;
            const double kWorldUpward = std::floor((kEyeUpward + kDotF(kUpward, kCenter)) / kTexel) * kTexel;
            const auto kCenterAcross = static_cast<float>(kWorldAcross - kEyeAcross);
            const auto kCenterUpward = static_cast<float>(kWorldUpward - kEyeUpward);
            const float kCenterAlong = kDotF(kAlong, kCenter);
            // Depth one toward the sun, at the shadows' distance before the
            // sphere; nought behind it.
            const float kNearest = kCenterAlong - kRadius - kSettings.distance;
            const float kFarthest = kCenterAlong + kRadius;
            const float kDepth = kFarthest - kNearest;
            Matrix& matrix = shadows.cascades[at].viewProjection;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                matrix[(axis * 4) + 0] = kAcross[axis] / kRadius;
                matrix[(axis * 4) + 1] = kUpward[axis] / kRadius;
                matrix[(axis * 4) + 2] = -kAlong[axis] / kDepth;
                matrix[(axis * 4) + 3] = 0;
            }
            matrix[12] = -kCenterAcross / kRadius;
            matrix[13] = -kCenterUpward / kRadius;
            matrix[14] = kFarthest / kDepth;
            matrix[15] = 1;
            shadows.cascades[at].far = kEnd;
            shadows.cascades[at].texel = kTexel;
            start = kEnd;
        }
    }

    const SceneFrame& queue(const SceneCamera& camera) {
        frame.draws.clear();
        frame.drawn = 0;
        frame.culled = 0;
        frame.hidden = 0;
        frame.malformed = 0;
        frame.unknownMeshes = 0;
        frame.overLimit = 0;
        frame.shadows.count = 0;
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
                           camera.fovY < std::numbers::pi_v<float> && camera.near > 0 && camera.aspect > 0;
        const auto [kRight, kUp, kForward] = axesOf(kSees ? camera.yaw : 0, kSees ? camera.pitch : 0);
        frame.view = Matrix{kRight[0],
                            kUp[0],
                            -kForward[0],
                            0,
                            kRight[1],
                            kUp[1],
                            -kForward[1],
                            0,
                            kRight[2],
                            kUp[2],
                            -kForward[2],
                            0,
                            0,
                            0,
                            0,
                            1};
        const float kHalf = kSees ? camera.fovY / 2 : 0.5F;
        const float kFocal = 1 / std::tan(kHalf);
        const float kAspect = kSees ? camera.aspect : 1.0F;
        const float kNear = kSees ? camera.near : 0.1F;
        // Reversed-Z with the far plane at infinity (ADR-0051): clip z is
        // the near distance and w the distance ahead, so depth is one at the
        // near plane and falls toward nought.
        frame.projection = Matrix{kFocal / kAspect, 0, 0, 0, 0, kFocal, 0, 0, 0, 0, 0, -1, 0, 0, kNear, 0};
        frame.exposure = std::isfinite(camera.exposure) ? camera.exposure : 15.0F;
        frame.metering = meteringOf(camera);
        frame.grading = gradingOf(camera.grading);
        frame.tonemapper = camera.tonemapper <= static_cast<std::uint32_t>(Tonemapper::Linear)
                               ? static_cast<Tonemapper>(camera.tonemapper)
                               : Tonemapper::Agx;
        frame.forward = kForward;
        const bool kShadows = kSees && settings.shadows.cascades > 0 && settings.shadows.side > 0 &&
                              settings.shadows.distance > kNear &&
                              (frame.lights.sun[0] > 0 || frame.lights.sun[1] > 0 || frame.lights.sun[2] > 0);
        if (kShadows) {
            cascades(camera, {kRight, kUp, kForward}, kHalf, kAspect, kNear);
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
            const float kRadius =
                kBounds.radius * std::max({std::abs(kScale[0]), std::abs(kScale[1]), std::abs(kScale[2])});
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
            if (const auto kBefore = placed.find(kKey); kBefore != placed.end()) {
                for (std::size_t column = 0; column < 3; ++column) {
                    for (std::size_t row = 0; row < 3; ++row) {
                        draw.previous[(column * 4) + row] = kBefore->second.turned[(column * 3) + row];
                    }
                    draw.previous[12 + column] =
                        static_cast<float>(kBefore->second.position[column] - camera.eye[column]);
                }
            }
            const Vector kColor = colorOf(kModel.color);
            draw.color = {kColor[0], kColor[1], kColor[2], static_cast<float>(kModel.color & 0xFFU) / 255.0F};
            // A model near enough casts into the shadows, seen or not: a
            // caster behind the eye still shades what is before it.
            const float kAway = std::sqrt((center[0] * center[0]) + (center[1] * center[1]) + (center[2] * center[2]));
            if (kLightShadows) {
                candidates.push_back({.draw = draw, .center = center, .radius = kRadius});
            }
            if (frame.shadows.count > 0 && kAway - kRadius <= frame.shadows.distance) {
                sunCandidates.push_back({.draw = draw, .center = center, .radius = kRadius});
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
            frame.draws.push_back(draw);
            ++frame.drawn;
        }
        castIntoCascades();
        temporal(camera, kSees);
        frame.metering.snap = frame.metering.enabled && (!meteredBefore || !continuous);
        meteredBefore = frame.metering.enabled;
        clusterLights(frame,
                      punctual,
                      camera,
                      {kRight, kUp, kForward},
                      {.sees = kSees, .half = kHalf, .aspect = kAspect, .near = kNear},
                      settings.limits,
                      shadowed);
        shadowLights(frame, shadowed, candidates, settings.lightShadows, settings.limits);
        return frame;
    }

    /// Each cascade's casters (D298): the models whose bounding spheres
    /// reach its box, which runs from the shadows' distance toward the sun
    /// to past its slice, in draw order, one after another; a model in two
    /// cascades is in both. Past the models' limit, the rest are left out.
    void castIntoCascades() {
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

    /// The temporal inputs (D291): the jitter, and the frame before's view
    /// taking this frame's places, unless this is the first or the eye cut.
    void temporal(const SceneCamera& camera, bool sees) {
        std::swap(placed, placing);
        placing.clear();
        SceneTemporal& now = frame.temporal;
        now.enabled = settings.antiAliasing == AntiAliasing::Taa && sees;
        frame.fxaa = settings.antiAliasing == AntiAliasing::Fxaa && sees;
        now.jitter = now.enabled ? temporalJitter(frames) : std::array<float, 2>{0, 0};
        const Matrix kViewProjection = times(frame.projection, frame.view);
        std::array<double, 3> moved{};
        for (std::size_t axis = 0; axis < 3 && previousEye.has_value(); ++axis) {
            moved[axis] = camera.eye[axis] - (*previousEye)[axis];
        }
        continuous = previousEye.has_value() && (moved[0] * moved[0]) + (moved[1] * moved[1]) + (moved[2] * moved[2]) <=
                                                    kCutDistance * kCutDistance;
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
};

Scene::Scene(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Scene::~Scene() = default;

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
    for (const std::uint64_t kId : {kBox, kSphere, kCylinder, kCapsule}) {
        state->meshes.emplace(kId, bounded(engineMesh(kId)));
    }
    // A game's mesh of an engine mesh's identity is the game's.
    for (const SceneMesh& kMesh : settings.meshes) {
        if (kMesh.mesh != nullptr && !kMesh.mesh->positions.empty()) {
            state->meshes.insert_or_assign(kMesh.id, bounded(kMesh.mesh));
        }
    }
    state->settings = std::move(settings);
    if (const auto kPose = registry.find(physics3d::Pose3D::kComponentTypeId)) {
        state->pose = *kPose;
    }
    return std::unique_ptr<Scene>{new Scene{std::move(state)}};
}

void Scene::extract(world::World& world) {
    State& state = *state_;
    state.extracted.clear();
    for (std::size_t component = 0; component < state.models.size(); ++component) {
        state.models[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                ModelInstance instance{.entity = chunk.entities[row],
                                       .component = static_cast<std::uint32_t>(component)};
                std::memcpy(&instance.model, chunk.columns[0] + (row * sizeof(Model)), sizeof(Model));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                state.extracted.push_back(instance);
            }
        });
    }
    // The first set, if any: a game declares at most one of each.
    const auto kFirstOf = [&world]<typename T>(std::optional<world::ColumnQuery>& query, std::optional<T>& into) {
        into.reset();
        if (!query.has_value()) {
            return;
        }
        query->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            if (!into.has_value() && !chunk.entities.empty()) {
                T value;
                std::memcpy(&value, chunk.columns[0], sizeof(T));
                into = value;
            }
        });
    };
    kFirstOf(state.sun, state.sunNow);
    kFirstOf(state.sky, state.skyNow);
    state.punctual.clear();
    for (auto& [query, spot] : state.lightQueries) {
        const bool kSpot = spot;
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                LightInstance instance{.entity = chunk.entities[row], .spot = kSpot};
                if (kSpot) {
                    std::memcpy(&instance.light, chunk.columns[0] + (row * sizeof(SpotLight)), sizeof(SpotLight));
                } else {
                    PointLight point;
                    std::memcpy(&point, chunk.columns[0] + (row * sizeof(PointLight)), sizeof(PointLight));
                    instance.light = SpotLight{
                        .lumens = point.lumens, .range = point.range, .color = point.color, .shadows = point.shadows};
                }
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                state.punctual.push_back(instance);
            }
        });
    }
}

const SceneFrame& Scene::queue(const SceneCamera& camera) {
    return state_->queue(camera);
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

std::span<const ModelInstance> Scene::extracted() const noexcept {
    return state_->extracted;
}

std::span<const LightInstance> Scene::extractedLights() const noexcept {
    return state_->punctual;
}

std::shared_ptr<const mesh::Mesh> Scene::mesh(std::uint64_t id) const {
    const auto kFound = state_->meshes.find(id);
    return kFound != state_->meshes.end() ? kFound->second.mesh : nullptr;
}

result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    GameScene loaded;
    const auto kOne = [](std::optional<schema::ComponentTypeId>& slot,
                         schema::ComponentTypeId id,
                         std::string_view why) -> result::Status {
        if (slot.has_value()) {
            return refuse(result::ErrorClass::InvalidArgument, RenderSceneError::BadComponents, why);
        }
        slot = id;
        return {};
    };
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (world_kest::ofEngineType(component, "rawframe.model.Model")) {
            loaded.models.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.Camera")) {
            RAWFRAME_TRY(kOne(loaded.camera, component.id, "a game has at most one 3D camera: a client has one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.AutoExposure")) {
            RAWFRAME_TRY(kOne(loaded.autoExposure, component.id, "a game has at most one auto-exposure: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Grading")) {
            RAWFRAME_TRY(kOne(loaded.grading, component.id, "a game has at most one grading: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sun")) {
            RAWFRAME_TRY(kOne(loaded.sun, component.id, "a game has at most one sun"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sky")) {
            RAWFRAME_TRY(kOne(loaded.sky, component.id, "a game has at most one sky"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.PointLight")) {
            loaded.points.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.SpotLight")) {
            loaded.spots.push_back(component.id);
        }
    }
    if (loaded.models.empty()) {
        return refuse(result::ErrorClass::NotFound, RenderSceneError::NoModels, "the game declares no models");
    }
    // By their full names: a game's own type called `Model` is not this one.
    const auto kLaidOut =
        [&program](bool declared,
                   std::string_view type,
                   std::size_t size,
                   std::initializer_list<std::pair<std::string_view, std::size_t>> fields) -> result::Status {
        if (declared && !world_kest::laidOutAs(program, type, size, fields)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "the program lays out a rawframe.model type otherwise than this engine reads it");
        }
        return {};
    };
    RAWFRAME_TRY(kLaidOut(true,
                          "rawframe.model.Model",
                          sizeof(Model),
                          {{"mesh", offsetof(Model, mesh)},
                           {"scaleX", offsetof(Model, scaleX)},
                           {"scaleY", offsetof(Model, scaleY)},
                           {"scaleZ", offsetof(Model, scaleZ)},
                           {"color", offsetof(Model, color)}}));
    RAWFRAME_TRY(kLaidOut(loaded.camera.has_value(),
                          "rawframe.model.Camera",
                          sizeof(Camera),
                          {{"offsetX", offsetof(Camera, offsetX)},
                           {"offsetY", offsetof(Camera, offsetY)},
                           {"offsetZ", offsetof(Camera, offsetZ)},
                           {"yaw", offsetof(Camera, yaw)},
                           {"pitch", offsetof(Camera, pitch)},
                           {"fovY", offsetof(Camera, fovY)},
                           {"near", offsetof(Camera, near)},
                           {"exposure", offsetof(Camera, exposure)},
                           {"tonemapper", offsetof(Camera, tonemapper)}}));
    RAWFRAME_TRY(kLaidOut(loaded.autoExposure.has_value(),
                          "rawframe.model.AutoExposure",
                          sizeof(AutoExposure),
                          {{"minimum", offsetof(AutoExposure, minimum)},
                           {"maximum", offsetof(AutoExposure, maximum)},
                           {"brighten", offsetof(AutoExposure, brighten)},
                           {"darken", offsetof(AutoExposure, darken)},
                           {"compensation", offsetof(AutoExposure, compensation)},
                           {"low", offsetof(AutoExposure, low)},
                           {"high", offsetof(AutoExposure, high)}}));
    RAWFRAME_TRY(kLaidOut(loaded.grading.has_value(),
                          "rawframe.model.Grading",
                          sizeof(Grading),
                          {{"slopeR", offsetof(Grading, slopeR)},
                           {"slopeG", offsetof(Grading, slopeG)},
                           {"slopeB", offsetof(Grading, slopeB)},
                           {"offsetR", offsetof(Grading, offsetR)},
                           {"offsetG", offsetof(Grading, offsetG)},
                           {"offsetB", offsetof(Grading, offsetB)},
                           {"powerR", offsetof(Grading, powerR)},
                           {"powerG", offsetof(Grading, powerG)},
                           {"powerB", offsetof(Grading, powerB)},
                           {"saturation", offsetof(Grading, saturation)},
                           {"contrast", offsetof(Grading, contrast)},
                           {"temperature", offsetof(Grading, temperature)},
                           {"tint", offsetof(Grading, tint)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sun.has_value(),
                          "rawframe.model.Sun",
                          sizeof(Sun),
                          {{"directionX", offsetof(Sun, directionX)},
                           {"directionY", offsetof(Sun, directionY)},
                           {"directionZ", offsetof(Sun, directionZ)},
                           {"illuminance", offsetof(Sun, illuminance)},
                           {"color", offsetof(Sun, color)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sky.has_value(),
                          "rawframe.model.Sky",
                          sizeof(Sky),
                          {{"luminance", offsetof(Sky, luminance)}, {"color", offsetof(Sky, color)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.points.empty(),
                          "rawframe.model.PointLight",
                          sizeof(PointLight),
                          {{"lumens", offsetof(PointLight, lumens)},
                           {"range", offsetof(PointLight, range)},
                           {"color", offsetof(PointLight, color)},
                           {"shadows", offsetof(PointLight, shadows)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.spots.empty(),
                          "rawframe.model.SpotLight",
                          sizeof(SpotLight),
                          {{"lumens", offsetof(SpotLight, lumens)},
                           {"range", offsetof(SpotLight, range)},
                           {"inner", offsetof(SpotLight, inner)},
                           {"outer", offsetof(SpotLight, outer)},
                           {"color", offsetof(SpotLight, color)},
                           {"shadows", offsetof(SpotLight, shadows)}}));
    for (const physics3d::BodyMesh& kMesh : game.meshes()) {
        loaded.meshes.push_back(SceneMesh{.id = kMesh.id, .mesh = kMesh.mesh});
    }
    return loaded;
}

} // namespace rawframe::render_scene
