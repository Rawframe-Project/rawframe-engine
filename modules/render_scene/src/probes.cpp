#include "probes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rawframe::render_scene {

void resolveProbes(SceneFrame& frame,
                   std::span<const ProbeInstance> probes,
                   const std::array<double, 3>& eye,
                   std::size_t maximum) {
    frame.probes.clear();
    frame.probesOverLimit = 0;
    struct Kept {
        const ProbeInstance* instance = nullptr;
        double distance = 0;
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
        double distance = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            distance += (each.position[axis] - eye[axis]) * (each.position[axis] - eye[axis]);
        }
        kept.push_back({.instance = &each, .distance = distance});
    }
    std::ranges::stable_sort(kept, {}, &Kept::distance);
    if (kept.size() > maximum) {
        frame.probesOverLimit = kept.size() - maximum;
        kept.resize(maximum);
    }
    for (const Kept& each : kept) {
        const ProbeInstance& kInstance = *each.instance;
        frame.probes.push_back(SceneProbe{.position = {static_cast<float>(kInstance.position[0] - eye[0]),
                                                       static_cast<float>(kInstance.position[1] - eye[1]),
                                                       static_cast<float>(kInstance.position[2] - eye[2])},
                                          .half = {kInstance.probe.halfX, kInstance.probe.halfY, kInstance.probe.halfZ},
                                          .environment = kInstance.probe.environment,
                                          .intensity = kInstance.probe.intensity});
    }
    for (SceneDraw& draw : frame.draws) {
        draw.probe = 0;
        std::uint32_t bestPriority = 0;
        float bestVolume = 0;
        for (std::size_t at = 0; at < frame.probes.size(); ++at) {
            const SceneProbe& kProbe = frame.probes[at];
            bool inside = true;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                inside = inside && std::abs(draw.model[12 + axis] - kProbe.position[axis]) <= kProbe.half[axis];
            }
            if (!inside) {
                continue;
            }
            const std::uint32_t kPriority = kept[at].instance->probe.priority;
            const float kVolume = kProbe.half[0] * kProbe.half[1] * kProbe.half[2];
            if (draw.probe == 0 || kPriority > bestPriority || (kPriority == bestPriority && kVolume < bestVolume)) {
                draw.probe = static_cast<std::uint32_t>(at + 1);
                bestPriority = kPriority;
                bestVolume = kVolume;
            }
        }
    }
}

} // namespace rawframe::render_scene
