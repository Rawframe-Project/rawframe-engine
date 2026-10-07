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
    // Ahead of the eye along its view (the view's third column, negated),
    // and the meters a share of the view's height spans per meter ahead:
    // twice the tangent of half its field, the projection's focal length's
    // inverse.
    const std::array<float, 3> kAhead = {-frame.view[2], -frame.view[6], -frame.view[10]};
    const float kSpan = frame.projection[5] > 0 ? 2 / frame.projection[5] : 0;
    for (const SceneLine& kLine : lines) {
        frame.particles.ribbons.push_back(
            particles::Ribbon{.material = kMaterial + (kLine.over ? 1U : 0U),
                              .first = static_cast<std::uint32_t>(frame.particles.ribbonPoints.size()),
                              .count = 2});
        for (const auto& [kAt, kAlong] : {std::pair{&kLine.from, 0.0F}, std::pair{&kLine.to, 1.0F}}) {
            const std::array<float, 3> kPlace = {static_cast<float>((*kAt)[0] - eye[0]),
                                                 static_cast<float>((*kAt)[1] - eye[1]),
                                                 static_cast<float>((*kAt)[2] - eye[2])};
            // A share of the view, held to what is a centimeter ahead.
            const float kDepth =
                std::max((kPlace[0] * kAhead[0]) + (kPlace[1] * kAhead[1]) + (kPlace[2] * kAhead[2]), 0.01F);
            frame.particles.ribbonPoints.push_back(
                particles::RibbonPoint{.place = kPlace,
                                       .width = kLine.ofView ? kLine.width * kDepth * kSpan : kLine.width,
                                       .color = kLine.color,
                                       .along = kAlong});
        }
    }
}

} // namespace rawframe::render_scene
