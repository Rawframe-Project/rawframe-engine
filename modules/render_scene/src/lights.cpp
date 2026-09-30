#include "lights.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <tuple>

namespace rawframe::render_scene {

namespace {

/// The slice a depth ahead falls in: nearer than the clusters' near is
/// the first, farther than their far the last, exponential between.
std::uint32_t sliceOf(const SceneClusters& clusters, float ahead) noexcept {
    const SceneClusters& kClusters = clusters;
    if (ahead <= kClusters.near) {
        return 0;
    }
    const float kSlice = std::log(ahead / kClusters.near) * static_cast<float>(kClusters.slices) /
                         std::log(kClusters.far / kClusters.near);
    return std::min(static_cast<std::uint32_t>(kSlice), kClusters.slices - 1);
}

/// A Morton index's column and row.
std::pair<std::uint32_t, std::uint32_t> mortonOf(std::uint64_t index) noexcept {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        x |= static_cast<std::uint32_t>((index >> (2 * bit)) & 1U) << bit;
        y |= static_cast<std::uint32_t>((index >> ((2 * bit) + 1)) & 1U) << bit;
    }
    return {x, y};
}

/// The models within a square's view and the light's reach, in draw
/// order, into its run of casters.
void cast(SceneLightShadows& out,
          ShadowSlot& slot,
          float range,
          std::span<const ShadowCandidate> candidates,
          const SceneLimits& limits) {
    slot.firstCaster = static_cast<std::uint32_t>(out.casters.size());
    const float kSlant = std::sqrt(1 + (slot.tangent * slot.tangent));
    for (const ShadowCandidate& kCandidate : candidates) {
        const Vector kFrom = {kCandidate.center[0] - slot.position[0],
                              kCandidate.center[1] - slot.position[1],
                              kCandidate.center[2] - slot.position[2]};
        const auto kDot = [&kFrom](const Vector& axis) {
            return (axis[0] * kFrom[0]) + (axis[1] * kFrom[1]) + (axis[2] * kFrom[2]);
        };
        const float kRadius = kCandidate.radius;
        const float kAhead = kDot(slot.forward);
        const float kAcross = kDot(slot.right);
        const float kUpward = kDot(slot.up);
        const float kAway = std::sqrt((kFrom[0] * kFrom[0]) + (kFrom[1] * kFrom[1]) + (kFrom[2] * kFrom[2]));
        if (kAway - kRadius > range || kAhead + kRadius < slot.near ||
            (kAcross - (slot.tangent * kAhead)) / kSlant > kRadius ||
            (-kAcross - (slot.tangent * kAhead)) / kSlant > kRadius ||
            (kUpward - (slot.tangent * kAhead)) / kSlant > kRadius ||
            (-kUpward - (slot.tangent * kAhead)) / kSlant > kRadius) {
            continue;
        }
        if (out.casters.size() == limits.maximumModels) {
            ++out.overLimit;
            continue;
        }
        out.casters.push_back(kCandidate.draw);
    }
    slot.casterCount = static_cast<std::uint32_t>(out.casters.size()) - slot.firstCaster;
}

} // namespace

ShadowSlot shadowSlotOf(const SceneLight& light, std::uint32_t face) noexcept {
    ShadowSlot slot{.position = light.position, .near = std::min(0.05F, light.range / 2)};
    Vector up{0, 1, 0};
    if (light.spot) {
        slot.forward = light.direction;
        slot.tangent = std::tan(std::min(std::acos(std::clamp(light.cosOuter, -1.0F, 1.0F)), 1.5F));
        up = std::abs(slot.forward[1]) < 0.99F ? Vector{0, 1, 0} : Vector{1, 0, 0};
    } else {
        constexpr std::array<Vector, 6> kFaces = {
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        slot.forward = kFaces[face];
        slot.tangent = 1;
        up = face == 2 || face == 3 ? Vector{0, 0, 1} : Vector{0, 1, 0};
    }
    slot.right = normalized(cross(slot.forward, up));
    slot.up = cross(slot.right, slot.forward);
    // Across and up over the distance ahead, depth the near plane over
    // it: reversed-Z with no far plane, as the view's.
    const auto kDot = [&slot](const Vector& axis) {
        return (axis[0] * slot.position[0]) + (axis[1] * slot.position[1]) + (axis[2] * slot.position[2]);
    };
    for (std::size_t column = 0; column < 3; ++column) {
        slot.viewProjection[(column * 4) + 0] = slot.right[column] / slot.tangent;
        slot.viewProjection[(column * 4) + 1] = slot.up[column] / slot.tangent;
        slot.viewProjection[(column * 4) + 3] = slot.forward[column];
    }
    slot.viewProjection[12] = -kDot(slot.right) / slot.tangent;
    slot.viewProjection[13] = -kDot(slot.up) / slot.tangent;
    slot.viewProjection[14] = slot.near;
    slot.viewProjection[15] = -kDot(slot.forward);
    return slot;
}

void clusterLights(SceneFrame& frame,
                   std::span<const LightInstance> punctual,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<bool>& shadowed) {
    const bool sees = view.sees;
    const float half = view.half;
    const float aspect = view.aspect;
    const float near = view.near;
    SceneClusters& clusters = frame.clusters;
    clusters.near = near;
    const std::uint32_t kCount = clusters.tilesX * clusters.tilesY * clusters.slices;
    clusters.ranges.assign(std::size_t{kCount} * 2, 0);
    clusters.indices.clear();
    shadowed.clear();
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
        if (frame.lights3d.size() == limits.maximumLights) {
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
            const std::array<Vector, 3> kTurn = turnOf(kTurned ? instance->rotation : std::array<float, 4>{0, 0, 0, 1});
            made.direction = normalized({-kTurn[2][0], -kTurn[2][1], -kTurn[2][2]});
            const float kOuter = std::clamp(kLight.outer, 0.0F, std::numbers::pi_v<float>);
            const float kInner = std::clamp(kLight.inner, 0.0F, kOuter);
            made.cosOuter = std::cos(kOuter);
            made.cosInner = std::max(std::cos(kInner), made.cosOuter + 0.0001F);
        }
        const auto kIndex = static_cast<std::uint32_t>(frame.lights3d.size());
        frame.lights3d.push_back(made);
        shadowed.push_back(kLight.shadows);
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
        for (std::uint32_t slice = sliceOf(clusters, kNearest); slice <= sliceOf(clusters, kFarthest); ++slice) {
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
            if (count == limits.maximumLightsPerCluster) {
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

void shadowLights(SceneFrame& frame,
                  const std::vector<bool>& shadowed,
                  std::span<const ShadowCandidate> candidates,
                  const LightShadowSettings& atlas,
                  const SceneLimits& limits) {
    SceneLightShadows& out = frame.lightShadows;
    out.side = 0;
    out.slots.clear();
    out.casters.clear();
    out.evicted = 0;
    out.overLimit = 0;
    const LightShadowSettings& kAtlas = atlas;
    std::vector<std::pair<float, std::uint32_t>> asking;
    for (std::uint32_t at = 0; at < frame.lights3d.size(); ++at) {
        if (!shadowed[at]) {
            continue;
        }
        const SceneLight& kLight = frame.lights3d[at];
        const float kAway =
            std::sqrt((kLight.position[0] * kLight.position[0]) + (kLight.position[1] * kLight.position[1]) +
                      (kLight.position[2] * kLight.position[2]));
        asking.emplace_back(kLight.range / std::max(kAway, kLight.range), at);
    }
    std::ranges::stable_sort(asking, std::greater{}, &std::pair<float, std::uint32_t>::first);
    const std::uint64_t kCells = kAtlas.smallest > 0 ? kAtlas.side / kAtlas.smallest : 0;
    std::uint64_t used = 0;
    // Light, side: in the order granted.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> granted;
    for (const auto& [kCover, kAt] : asking) {
        if (granted.size() == limits.maximumShadowedLights || kCells == 0) {
            ++out.evicted;
            continue;
        }
        std::uint32_t side = kAtlas.largest;
        for (float earned = 0.5F; side > kAtlas.smallest && kCover < earned; earned /= 2) {
            side /= 2;
        }
        const std::uint64_t kFaces = frame.lights3d[kAt].spot ? 1 : 6;
        while (side >= kAtlas.smallest &&
               used + (kFaces * (side / kAtlas.smallest) * (side / kAtlas.smallest)) > kCells * kCells) {
            side /= 2;
        }
        if (side < kAtlas.smallest) {
            ++out.evicted;
            continue;
        }
        used += kFaces * (side / kAtlas.smallest) * (side / kAtlas.smallest);
        granted.emplace_back(kAt, side);
    }
    if (granted.empty()) {
        return;
    }
    out.side = kAtlas.side;
    std::ranges::stable_sort(granted, std::greater{}, &std::pair<std::uint32_t, std::uint32_t>::second);
    std::uint64_t cursor = 0;
    for (const auto& [kAt, kSide] : granted) {
        SceneLight& light = frame.lights3d[kAt];
        light.shadowSlot = static_cast<std::uint32_t>(out.slots.size());
        light.shadowSlots = light.spot ? 1 : 6;
        for (std::uint32_t face = 0; face < light.shadowSlots; ++face) {
            const auto [kX, kY] = mortonOf(cursor);
            cursor += std::uint64_t{kSide / kAtlas.smallest} * (kSide / kAtlas.smallest);
            ShadowSlot slot = shadowSlotOf(light, face);
            slot.x = kX * kAtlas.smallest;
            slot.y = kY * kAtlas.smallest;
            slot.side = kSide;
            cast(out, slot, light.range, candidates, limits);
            out.slots.push_back(slot);
        }
    }
}

} // namespace rawframe::render_scene
