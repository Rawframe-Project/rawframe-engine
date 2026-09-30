#include "rawframe/render_scene/scene.h"

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <tuple>

namespace rawframe::render_scene {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderSceneError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderSceneDomain, code(error), why).error()};
}

using Vector = std::array<float, 3>;

Vector cross(const Vector& a, const Vector& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

Vector normalized(const Vector& a) noexcept {
    const float kLength = std::sqrt((a[0] * a[0]) + (a[1] * a[1]) + (a[2] * a[2]));
    return kLength > 0 ? Vector{a[0] / kLength, a[1] / kLength, a[2] / kLength} : Vector{0, 1, 0};
}

/// An sRGB channel of 0xRRGGBBAA, `shift` bits up, in linear light.
float linearOf(std::uint32_t color, unsigned shift) noexcept {
    const float kEncoded = static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    return kEncoded <= 0.04045F ? kEncoded / 12.92F : std::pow((kEncoded + 0.055F) / 1.055F, 2.4F);
}

Vector colorOf(std::uint32_t color) noexcept {
    return {linearOf(color, 24), linearOf(color, 16), linearOf(color, 8)};
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

/// The columns of a unit quaternion's turn.
std::array<Vector, 3> turnOf(const std::array<float, 4>& q) noexcept {
    const float kX = q[0];
    const float kY = q[1];
    const float kZ = q[2];
    const float kW = q[3];
    return {{{1 - (2 * ((kY * kY) + (kZ * kZ))), 2 * ((kX * kY) + (kZ * kW)), 2 * ((kX * kZ) - (kY * kW))},
             {2 * ((kX * kY) - (kZ * kW)), 1 - (2 * ((kX * kX) + (kZ * kZ))), 2 * ((kY * kZ) + (kX * kW))},
             {2 * ((kX * kZ) + (kY * kW)), 2 * ((kY * kZ) - (kX * kW)), 1 - (2 * ((kX * kX) + (kY * kY)))}}};
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
            const Vector kColor = colorOf(kModel.color);
            draw.color = {kColor[0], kColor[1], kColor[2], static_cast<float>(kModel.color & 0xFFU) / 255.0F};
            // A model near enough casts into the shadows, seen or not: a
            // caster behind the eye still shades what is before it.
            const float kAway = std::sqrt((center[0] * center[0]) + (center[1] * center[1]) + (center[2] * center[2]));
            if (frame.shadows.count > 0 && kAway - kRadius <= frame.shadows.distance) {
                if (frame.shadows.casters.size() < settings.limits.maximumModels) {
                    frame.shadows.casters.push_back(draw);
                } else {
                    ++frame.shadows.overLimit;
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
            frame.draws.push_back(draw);
            ++frame.drawn;
        }
        cluster(camera, {kRight, kUp, kForward}, kSees, kHalf, kAspect, kNear);
        return frame;
    }

    /// The slice a depth ahead falls in: nearer than the clusters' near is
    /// the first, farther than their far the last, exponential between.
    [[nodiscard]] std::uint32_t sliceOf(float ahead) const noexcept {
        const SceneClusters& kClusters = frame.clusters;
        if (ahead <= kClusters.near) {
            return 0;
        }
        const float kSlice = std::log(ahead / kClusters.near) * static_cast<float>(kClusters.slices) /
                             std::log(kClusters.far / kClusters.near);
        return std::min(static_cast<std::uint32_t>(kSlice), kClusters.slices - 1);
    }

    /// The view stage's lights (ADR-0051, D290): each punctual light that
    /// lights something and reaches the view, in the order of its entity,
    /// named by every cluster its sphere may reach: the slices its depth
    /// spans, and the tiles the box around it covers on the screen.
    void cluster(
        const SceneCamera& camera, const std::array<Vector, 3>& axes, bool sees, float half, float aspect, float near) {
        SceneClusters& clusters = frame.clusters;
        clusters.near = near;
        const std::uint32_t kCount = clusters.tilesX * clusters.tilesY * clusters.slices;
        clusters.ranges.assign(std::size_t{kCount} * 2, 0);
        clusters.indices.clear();
        std::vector<const LightInstance*> ordered;
        for (const LightInstance& light : punctual) {
            ordered.push_back(&light);
        }
        std::ranges::sort(ordered, [](const LightInstance* left, const LightInstance* right) {
            return std::tuple{left->entity, left->spot} < std::tuple{right->entity, right->spot};
        });
        const auto& [kRight, kUp, kForward] = axes;
        const float kTanY = std::tan(half);
        const float kTanX = kTanY * aspect;
        // Cluster, light: sorted by cluster, then named in order.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> named;
        for (const LightInstance* instance : ordered) {
            const SpotLight& kLight = instance->light;
            const bool kFinite = std::isfinite(kLight.lumens) && std::isfinite(kLight.range) &&
                                 std::isfinite(kLight.inner) && std::isfinite(kLight.outer) &&
                                 std::ranges::all_of(instance->position,
                                                     [](double value) {
                                                         return std::isfinite(value);
                                                     }) &&
                                 std::ranges::all_of(instance->rotation, [](float value) {
                                     return std::isfinite(value);
                                 });
            if (!sees || !kFinite || kLight.lumens <= 0 || kLight.range <= 0) {
                ++frame.lightsCulled;
                continue;
            }
            const Vector kPlace = {static_cast<float>(instance->position[0] - camera.eye[0]),
                                   static_cast<float>(instance->position[1] - camera.eye[1]),
                                   static_cast<float>(instance->position[2] - camera.eye[2])};
            const auto kDot = [&kPlace](const Vector& axis) {
                return (axis[0] * kPlace[0]) + (axis[1] * kPlace[1]) + (axis[2] * kPlace[2]);
            };
            const float kAcross = kDot(kRight);
            const float kUpward = kDot(kUp);
            const float kAhead = kDot(kForward);
            const float kRange = kLight.range;
            // Behind the near plane, or past a side of the view, wholly.
            const float kWide = std::atan(kTanX);
            const bool kBehind = kAhead + kRange < near;
            const bool kPast = (kUpward * std::cos(half)) - (kAhead * std::sin(half)) > kRange ||
                               (-kUpward * std::cos(half)) - (kAhead * std::sin(half)) > kRange ||
                               (kAcross * std::cos(kWide)) - (kAhead * std::sin(kWide)) > kRange ||
                               (-kAcross * std::cos(kWide)) - (kAhead * std::sin(kWide)) > kRange;
            if (kBehind || kPast) {
                ++frame.lightsCulled;
                continue;
            }
            if (frame.lights3d.size() == settings.limits.maximumLights) {
                ++frame.lightsOverLimit;
                continue;
            }
            const Vector kColor = colorOf(kLight.color);
            // Lumens to candela (ADR-0051): over the sphere for a point,
            // over π for a spot.
            const float kCandela =
                kLight.lumens / (instance->spot ? std::numbers::pi_v<float> : 4 * std::numbers::pi_v<float>);
            SceneLight made{.position = kPlace,
                            .range = kRange,
                            .intensity = {kColor[0] * kCandela, kColor[1] * kCandela, kColor[2] * kCandela},
                            .spot = instance->spot};
            if (instance->spot) {
                const bool kTurned = std::ranges::any_of(instance->rotation, [](float value) {
                    return value != 0;
                });
                const std::array<Vector, 3> kTurn =
                    turnOf(kTurned ? instance->rotation : std::array<float, 4>{0, 0, 0, 1});
                made.direction = normalized({-kTurn[2][0], -kTurn[2][1], -kTurn[2][2]});
                const float kOuter = std::clamp(kLight.outer, 0.0F, std::numbers::pi_v<float>);
                const float kInner = std::clamp(kLight.inner, 0.0F, kOuter);
                made.cosOuter = std::cos(kOuter);
                made.cosInner = std::max(std::cos(kInner), made.cosOuter + 0.0001F);
            }
            const auto kIndex = static_cast<std::uint32_t>(frame.lights3d.size());
            frame.lights3d.push_back(made);
            // Its slices, and the tiles between the lines from the eye that
            // touch its sphere across and up; one about the eye covers all.
            const float kNearest = std::max(kAhead - kRange, near);
            const float kFarthest = std::max(kAhead + kRange, kNearest);
            const auto kTouching = [kAhead, kRange](float along, float tangent) {
                if (kAhead <= kRange) {
                    return std::pair{-1.0F, 1.0F};
                }
                const float kSquare = (kAhead * kAhead) - (kRange * kRange);
                const float kSpread = kRange * std::sqrt((along * along) + kSquare);
                return std::pair{((along * kAhead) - kSpread) / kSquare / tangent,
                                 ((along * kAhead) + kSpread) / kSquare / tangent};
            };
            const auto [left, right] = kTouching(kAcross, kTanX);
            const auto [bottom, top] = kTouching(kUpward, kTanY);
            const auto kTile = [](float ndc, bool downward, std::uint32_t tiles) {
                const float kAt = (downward ? 0.5F - (ndc * 0.5F) : (ndc * 0.5F) + 0.5F) * static_cast<float>(tiles);
                return static_cast<std::uint32_t>(std::clamp(kAt, 0.0F, static_cast<float>(tiles - 1)));
            };
            const std::uint32_t kX0 = kTile(left, false, clusters.tilesX);
            const std::uint32_t kX1 = kTile(right, false, clusters.tilesX);
            const std::uint32_t kY0 = kTile(top, true, clusters.tilesY);
            const std::uint32_t kY1 = kTile(bottom, true, clusters.tilesY);
            for (std::uint32_t slice = sliceOf(kNearest); slice <= sliceOf(kFarthest); ++slice) {
                for (std::uint32_t y = kY0; y <= kY1; ++y) {
                    for (std::uint32_t x = kX0; x <= kX1; ++x) {
                        named.emplace_back((((slice * clusters.tilesY) + y) * clusters.tilesX) + x, kIndex);
                    }
                }
            }
        }
        std::ranges::stable_sort(named, {}, &std::pair<std::uint32_t, std::uint32_t>::first);
        for (std::size_t at = 0; at < named.size();) {
            const std::uint32_t kCluster = named[at].first;
            const auto kFirst = static_cast<std::uint32_t>(clusters.indices.size());
            std::uint32_t count = 0;
            for (; at < named.size() && named[at].first == kCluster; ++at) {
                if (count == settings.limits.maximumLightsPerCluster) {
                    ++frame.clusterOverflow;
                    continue;
                }
                clusters.indices.push_back(named[at].second);
                ++count;
            }
            clusters.ranges[std::size_t{kCluster} * 2] = kFirst;
            clusters.ranges[(std::size_t{kCluster} * 2) + 1] = count;
        }
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
                    instance.light = SpotLight{.lumens = point.lumens, .range = point.range, .color = point.color};
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
                           {"exposure", offsetof(Camera, exposure)}}));
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
                           {"color", offsetof(PointLight, color)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.spots.empty(),
                          "rawframe.model.SpotLight",
                          sizeof(SpotLight),
                          {{"lumens", offsetof(SpotLight, lumens)},
                           {"range", offsetof(SpotLight, range)},
                           {"inner", offsetof(SpotLight, inner)},
                           {"outer", offsetof(SpotLight, outer)},
                           {"color", offsetof(SpotLight, color)}}));
    for (const physics3d::BodyMesh& kMesh : game.meshes()) {
        loaded.meshes.push_back(SceneMesh{.id = kMesh.id, .mesh = kMesh.mesh});
    }
    return loaded;
}

} // namespace rawframe::render_scene
