#include "bounded.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rawframe::render_scene {

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
    std::vector<Run> runs;
    for (const mesh::Part& kPart : made->parts) {
        if (!runs.empty() && runs.back().material == kPart.material) {
            runs.back().indexCount += kPart.indexCount;
        } else {
            runs.push_back(
                {.firstIndex = kPart.firstIndex, .indexCount = kPart.indexCount, .material = kPart.material});
        }
    }
    return Bounded{.mesh = std::move(made), .center = kCenter, .radius = radius, .runs = std::move(runs)};
}

} // namespace rawframe::render_scene
