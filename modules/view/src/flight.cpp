#include "rawframe/view/flight.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawframe::view {

namespace {

// A pixel of drag turns the view a two hundredth of a radian, as the
// session's orbit does; a detent moves a fifth of the way.
constexpr double kTurnPerPixel = 0.005;
constexpr double kDollyPerDetent = 0.2;
constexpr double kFaster = 3;

std::array<double, 3> along(const std::array<float, 3>& axis, double by) noexcept {
    return {axis[0] * by, axis[1] * by, axis[2] * by};
}

bool finite(const Perspective& view) noexcept {
    return std::isfinite(view.yaw) && std::isfinite(view.pitch) && std::ranges::all_of(view.eye, [](double each) {
               return std::isfinite(each);
           });
}

} // namespace

bool steers(const Steering& steering) noexcept {
    return steering.forward || steering.back || steering.left || steering.right || steering.down || steering.up ||
           steering.turn != std::array<double, 2>{} || steering.pan != std::array<double, 2>{} ||
           steering.orbit != std::array<double, 2>{} || steering.wheel != 0 || steering.speedWheel != 0;
}

std::array<double, 3> pivotOf(const Perspective& view, const Flight& flight) noexcept {
    const Axes kAxes = axesOf(view.yaw, view.pitch);
    const auto kAhead = along(kAxes.forward, flight.pivot);
    return {view.eye[0] + kAhead[0], view.eye[1] + kAhead[1], view.eye[2] + kAhead[2]};
}

Perspective
flown(const Perspective& view, Flight& flight, const Steering& steering, double seconds, double height) noexcept {
    constexpr double kSteepest = (std::numbers::pi / 2) - 0.01;
    Perspective moved = view;
    // Orbited: about the point ahead, which stays where it is.
    if (steering.orbit != std::array<double, 2>{}) {
        const std::array<double, 3> kPivot = pivotOf(moved, flight);
        moved.yaw = static_cast<float>(moved.yaw - (steering.orbit[0] * kTurnPerPixel));
        moved.pitch =
            static_cast<float>(std::clamp(moved.pitch - (steering.orbit[1] * kTurnPerPixel), -kSteepest, kSteepest));
        const auto kBack = along(axesOf(moved.yaw, moved.pitch).forward, flight.pivot);
        moved.eye = {kPivot[0] - kBack[0], kPivot[1] - kBack[1], kPivot[2] - kBack[2]};
    }
    // Turned about the eye.
    if (steering.turn != std::array<double, 2>{}) {
        moved.yaw = static_cast<float>(moved.yaw - (steering.turn[0] * kTurnPerPixel));
        moved.pitch =
            static_cast<float>(std::clamp(moved.pitch - (steering.turn[1] * kTurnPerPixel), -kSteepest, kSteepest));
    }
    const Axes kAxes = axesOf(moved.yaw, moved.pitch);
    // Panned: what is at the point ahead stays under the pointer.
    if (steering.pan != std::array<double, 2>{} && height > 0) {
        const double kPerPixel = 2 * flight.pivot * std::tan(moved.fovY / 2.0) / height;
        const auto kAcross = along(kAxes.right, -steering.pan[0] * kPerPixel);
        const auto kUp = along(kAxes.up, steering.pan[1] * kPerPixel);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            moved.eye[axis] += kAcross[axis] + kUp[axis];
        }
    }
    // The wheel toward the author moves away, as a page scrolls.
    if (steering.wheel != 0) {
        const double kBy = flight.pivot * kDollyPerDetent * steering.wheel;
        const auto kBack = along(kAxes.forward, -kBy);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            moved.eye[axis] += kBack[axis];
        }
        flight.pivot = std::clamp(flight.pivot + kBy, 0.25, 1e5);
    }
    if (steering.speedWheel != 0) {
        flight.speed = std::clamp(flight.speed * std::pow(1.25, -steering.speedWheel), 0.25, 1e4);
    }
    const double kAhead = (steering.forward ? 1.0 : 0.0) - (steering.back ? 1.0 : 0.0);
    const double kAcross = (steering.right ? 1.0 : 0.0) - (steering.left ? 1.0 : 0.0);
    const double kRise = (steering.up ? 1.0 : 0.0) - (steering.down ? 1.0 : 0.0);
    if ((kAhead != 0 || kAcross != 0 || kRise != 0) && seconds > 0) {
        const double kStep = flight.speed * (steering.faster ? kFaster : 1.0) * std::min(seconds, 0.25);
        const auto kForward = along(kAxes.forward, kAhead * kStep);
        const auto kRight = along(kAxes.right, kAcross * kStep);
        moved.eye[0] += kForward[0] + kRight[0];
        moved.eye[1] += kForward[1] + kRight[1] + (kRise * kStep);
        moved.eye[2] += kForward[2] + kRight[2];
    }
    return finite(moved) ? moved : view;
}

} // namespace rawframe::view
