#include "clusters.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rawframe::render_scene {

namespace {

/// The slice a depth ahead falls in: nearer than the clusters' near is
/// the first, farther than their far the last, exponential between.
std::uint32_t sliceOf(const SceneClusters& clusters, float ahead) noexcept {
    if (ahead <= clusters.near) {
        return 0;
    }
    const float kSlice =
        std::log(ahead / clusters.near) * static_cast<float>(clusters.slices) / std::log(clusters.far / clusters.near);
    return std::min(static_cast<std::uint32_t>(kSlice), clusters.slices - 1);
}

} // namespace

bool outsideView(const std::array<Vector, 3>& axes,
                 const ViewShape& view,
                 const Vector& center,
                 float radius) noexcept {
    const auto& [kRight, kUp, kForward] = axes;
    const auto kDot = [&center](const Vector& axis) {
        return (axis[0] * center[0]) + (axis[1] * center[1]) + (axis[2] * center[2]);
    };
    const float kAcross = kDot(kRight);
    const float kUpward = kDot(kUp);
    const float kAhead = kDot(kForward);
    const float kWide = std::atan(std::tan(view.half) * view.aspect);
    return kAhead + radius < view.near || (kUpward * std::cos(view.half)) - (kAhead * std::sin(view.half)) > radius ||
           (-kUpward * std::cos(view.half)) - (kAhead * std::sin(view.half)) > radius ||
           (kAcross * std::cos(kWide)) - (kAhead * std::sin(kWide)) > radius ||
           (-kAcross * std::cos(kWide)) - (kAhead * std::sin(kWide)) > radius;
}

void nameSphere(const SceneClusters& clusters,
                const std::array<Vector, 3>& axes,
                const ViewShape& view,
                const Vector& center,
                float radius,
                ClusterItem item,
                std::uint32_t index,
                std::vector<ClusterName>& named) {
    const auto& [kRight, kUp, kForward] = axes;
    const auto kDot = [&center](const Vector& axis) {
        return (axis[0] * center[0]) + (axis[1] * center[1]) + (axis[2] * center[2]);
    };
    const float kAcross = kDot(kRight);
    const float kUpward = kDot(kUp);
    const float kAhead = kDot(kForward);
    const float kTanY = std::tan(view.half);
    const float kTanX = kTanY * view.aspect;
    const float kNearest = std::max(kAhead - radius, view.near);
    const float kFarthest = std::max(kAhead + radius, kNearest);
    const auto kTouching = [kAhead, radius](float along, float tangent) {
        if (kAhead <= radius) {
            return std::pair{-1.0F, 1.0F};
        }
        const float kSquare = (kAhead * kAhead) - (radius * radius);
        const float kSpread = radius * std::sqrt((along * along) + kSquare);
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
                named.push_back(
                    {.cluster = (((slice * clusters.tilesY) + y) * clusters.tilesX) + x, .item = item, .index = index});
            }
        }
    }
}

void packClusters(SceneFrame& frame, std::vector<ClusterName>& named, const SceneLimits& limits) {
    SceneClusters& clusters = frame.clusters;
    const std::uint32_t kCount = clusters.tilesX * clusters.tilesY * clusters.slices;
    clusters.ranges.assign(std::size_t{kCount} * 4, 0);
    clusters.indices.clear();
    const std::array<std::size_t, 3> kLimits = {
        limits.maximumLightsPerCluster, limits.maximumDecalsPerCluster, limits.maximumProbesPerCluster};
    // Placed by counting rather than sorted (D537): each class of each
    // cluster counted, the first named up to its limit, so each one's place
    // is known, then each name put in its place in the order named. A
    // stable sort of every name each frame was a tenth of a client's own
    // time.
    for (const ClusterName& name : named) {
        std::uint32_t& count =
            clusters.ranges[(std::size_t{name.cluster} * 4) + 1 + static_cast<std::size_t>(name.item)];
        if (count == kLimits[static_cast<std::size_t>(name.item)]) {
            ++frame.clusterOverflow;
        } else {
            ++count;
        }
    }
    std::vector<std::uint32_t> next(std::size_t{kCount} * 3);
    std::uint32_t placed = 0;
    for (std::size_t cluster = 0; cluster < kCount; ++cluster) {
        clusters.ranges[cluster * 4] = placed;
        for (std::size_t kind = 0; kind < 3; ++kind) {
            next[(cluster * 3) + kind] = placed;
            placed += clusters.ranges[(cluster * 4) + 1 + kind];
        }
    }
    clusters.indices.resize(placed);
    for (const ClusterName& name : named) {
        const std::size_t kCluster = name.cluster;
        const auto kKind = static_cast<std::size_t>(name.item);
        std::uint32_t& at = next[(kCluster * 3) + kKind];
        // Its class's end: past it, the name was one over the limit.
        std::uint32_t end = clusters.ranges[kCluster * 4];
        for (std::size_t kind = 0; kind <= kKind; ++kind) {
            end += clusters.ranges[(kCluster * 4) + 1 + kind];
        }
        if (at < end) {
            clusters.indices[at] = name.index;
            ++at;
        }
    }
}

} // namespace rawframe::render_scene
