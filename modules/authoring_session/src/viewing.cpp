#include "viewing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawframe::authoring_session {

authoring::SceneView movedView(const authoring::SceneView& view, double detents, double across, double up) noexcept {
    constexpr double kPerPixel = 1.0 / 200;
    constexpr double kHighest = (std::numbers::pi / 2) - 0.05;
    const std::array<double, 3> kFrom = {
        view.eye[0] - view.target[0], view.eye[1] - view.target[1], view.eye[2] - view.target[2]};
    const double kDistance = std::hypot(kFrom[0], kFrom[1], kFrom[2]);
    if (!std::isfinite(kDistance) || kDistance <= 0) {
        return view;
    }
    const double kYaw = std::atan2(kFrom[0], kFrom[2]) - (across * kPerPixel);
    const double kPitch =
        std::clamp(std::asin(std::clamp(kFrom[1] / kDistance, -1.0, 1.0)) + (up * kPerPixel), -kHighest, kHighest);
    const double kFar = std::clamp(kDistance * std::pow(1 + (1.0 / 6), detents), 0.5, 1e4);
    authoring::SceneView moved = view;
    moved.eye = {view.target[0] + (kFar * std::cos(kPitch) * std::sin(kYaw)),
                 view.target[1] + (kFar * std::sin(kPitch)),
                 view.target[2] + (kFar * std::cos(kPitch) * std::cos(kYaw))};
    return moved;
}

} // namespace rawframe::authoring_session
