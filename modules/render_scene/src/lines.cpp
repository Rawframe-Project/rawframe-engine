#include "lines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rawframe::render_scene {

std::vector<SceneLine> soundLines(std::span<const SceneLine> lines) {
    const auto kFinite = [](const auto& values) {
        return std::ranges::all_of(values, [](auto each) {
            return std::isfinite(each);
        });
    };
    std::vector<SceneLine> kept;
    for (const SceneLine& kLine : lines) {
        if (kept.size() >= kMostSceneLines) {
            break;
        }
        if (kFinite(kLine.from) && kFinite(kLine.to) && kFinite(kLine.color) && std::isfinite(kLine.width) &&
            kLine.width > 0) {
            kept.push_back(kLine);
        }
    }
    return kept;
}

void addLines(SceneFrame& frame, std::span<const SceneLine> lines, const std::array<double, 3>& eye) {
    const auto kMaterial = static_cast<std::uint32_t>(frame.materials.size());
    for (const SceneLine& kLine : lines) {
        frame.particles.ribbons.push_back(
            particles::Ribbon{.material = kMaterial,
                              .first = static_cast<std::uint32_t>(frame.particles.ribbonPoints.size()),
                              .count = 2});
        for (const auto& [kAt, kAlong] : {std::pair{&kLine.from, 0.0F}, std::pair{&kLine.to, 1.0F}}) {
            frame.particles.ribbonPoints.push_back(
                particles::RibbonPoint{.place = {static_cast<float>((*kAt)[0] - eye[0]),
                                                 static_cast<float>((*kAt)[1] - eye[1]),
                                                 static_cast<float>((*kAt)[2] - eye[2])},
                                       .width = kLine.width,
                                       .color = kLine.color,
                                       .along = kAlong});
        }
    }
}

} // namespace rawframe::render_scene
