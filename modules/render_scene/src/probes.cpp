#include "probes.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace rawframe::render_scene {

void clusterProbes(SceneFrame& frame,
                   std::span<const ProbeInstance> probes,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<ClusterName>& named) {
    frame.probes.clear();
    frame.probesOverLimit = 0;
    struct Kept {
        const ProbeInstance* instance = nullptr;
        double distance = 0;
        Vector center{};
        float radius = 0;
    };
    std::vector<Kept> kept;
    for (const ProbeInstance& each : probes) {
        const ReflectionProbe& kProbe = each.probe;
        const bool kFinite = std::isfinite(kProbe.halfX) && std::isfinite(kProbe.halfY) &&
                             std::isfinite(kProbe.halfZ) && std::isfinite(kProbe.intensity) &&
                             std::ranges::all_of(each.position, [](double axis) {
                                 return std::isfinite(axis);
                             });
        if (!kFinite || kProbe.halfX <= 0 || kProbe.halfY <= 0 || kProbe.halfZ <= 0 || kProbe.environment == 0) {
            continue;
        }
        const Vector kCenter = {static_cast<float>(each.position[0] - camera.eye[0]),
                                static_cast<float>(each.position[1] - camera.eye[1]),
                                static_cast<float>(each.position[2] - camera.eye[2])};
        // Its box grown by a tenth, across which it fades out (D340).
        const float kRadius = 1.1F * std::sqrt((kProbe.halfX * kProbe.halfX) + (kProbe.halfY * kProbe.halfY) +
                                               (kProbe.halfZ * kProbe.halfZ));
        if (!view.sees || outsideView(axes, view, kCenter, kRadius)) {
            continue;
        }
        double distance = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            distance += (each.position[axis] - camera.eye[axis]) * (each.position[axis] - camera.eye[axis]);
        }
        kept.push_back({.instance = &each, .distance = distance, .center = kCenter, .radius = kRadius});
    }
    std::ranges::stable_sort(kept, {}, &Kept::distance);
    if (kept.size() > limits.maximumProbes) {
        frame.probesOverLimit = kept.size() - limits.maximumProbes;
        kept.resize(limits.maximumProbes);
    }
    // As a point takes them: the highest priority, then the smaller box,
    // then the nearer, which is the order they are in.
    const auto kRank = [](const Kept& each) {
        const ReflectionProbe& kProbe = each.instance->probe;
        return std::tuple{-static_cast<std::int64_t>(kProbe.priority), kProbe.halfX * kProbe.halfY * kProbe.halfZ};
    };
    std::ranges::stable_sort(kept, {}, kRank);
    for (const Kept& each : kept) {
        const ReflectionProbe& kProbe = each.instance->probe;
        const auto kIndex = static_cast<std::uint32_t>(frame.probes.size());
        frame.probes.push_back(SceneProbe{.position = each.center,
                                          .half = {kProbe.halfX, kProbe.halfY, kProbe.halfZ},
                                          .environment = kProbe.environment,
                                          .intensity = kProbe.intensity});
        nameSphere(frame.clusters, axes, view, each.center, each.radius, ClusterItem::Probe, kIndex, named);
    }
}

void withoutProbes(SceneFrame& frame) noexcept {
    frame.probes.clear();
    frame.probesOverLimit = 0;
    for (std::size_t at = 3; at < frame.clusters.ranges.size(); at += 4) {
        frame.clusters.ranges[at] = 0;
    }
}

} // namespace rawframe::render_scene
