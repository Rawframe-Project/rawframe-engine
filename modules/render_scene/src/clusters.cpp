#include "clusters.h"

#include <algorithm>
#include <cmath>
#include <tuple>

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
    std::ranges::stable_sort(named, {}, [](const ClusterName& name) {
        return std::tuple{name.cluster, name.item};
    });
    for (std::size_t at = 0; at < named.size();) {
        const std::uint32_t kCluster = named[at].cluster;
        clusters.ranges[std::size_t{kCluster} * 4] = static_cast<std::uint32_t>(clusters.indices.size());
        for (; at < named.size() && named[at].cluster == kCluster; ++at) {
            const bool kLight = named[at].item == ClusterItem::Light;
            std::uint32_t& count = clusters.ranges[(std::size_t{kCluster} * 4) + (kLight ? 1 : 2)];
            if (count == (kLight ? limits.maximumLightsPerCluster : limits.maximumDecalsPerCluster)) {
                ++frame.clusterOverflow;
                continue;
            }
            clusters.indices.push_back(named[at].index);
            ++count;
        }
    }
}

} // namespace rawframe::render_scene
