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

authoring::SceneView lookedView(const authoring::SceneView& view, double across, double up) noexcept {
    constexpr double kPerPixel = 1.0 / 200;
    constexpr double kHighest = (std::numbers::pi / 2) - 0.05;
    const std::array<double, 3> kAhead = {
        view.target[0] - view.eye[0], view.target[1] - view.eye[1], view.target[2] - view.eye[2]};
    const double kDistance = std::hypot(kAhead[0], kAhead[1], kAhead[2]);
    if (!std::isfinite(kDistance) || kDistance <= 0) {
        return view;
    }
    const double kYaw = std::atan2(kAhead[0], kAhead[2]) - (across * kPerPixel);
    const double kPitch =
        std::clamp(std::asin(std::clamp(kAhead[1] / kDistance, -1.0, 1.0)) - (up * kPerPixel), -kHighest, kHighest);
    authoring::SceneView looked = view;
    looked.target = {view.eye[0] + (kDistance * std::cos(kPitch) * std::sin(kYaw)),
                     view.eye[1] + (kDistance * std::sin(kPitch)),
                     view.eye[2] + (kDistance * std::cos(kPitch) * std::cos(kYaw))};
    return looked;
}

authoring::SceneView flownView(const authoring::SceneView& view, double detents) noexcept {
    const std::array<double, 3> kAhead = {
        view.target[0] - view.eye[0], view.target[1] - view.eye[1], view.target[2] - view.eye[2]};
    // A sixth of the distance per detent, back toward the author.
    const double kBy = -detents / 6;
    if (!std::isfinite(kBy)) {
        return view;
    }
    authoring::SceneView flown = view;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        flown.eye[axis] += kAhead[axis] * kBy;
        flown.target[axis] += kAhead[axis] * kBy;
    }
    return flown;
}

namespace {

std::array<double, 2> pairOf(const document::Value* value) {
    if (value == nullptr || value->kind() != document::Value::Kind::Array || value->items().size() != 2) {
        return {};
    }
    return {value->items()[0].real().value_or(0), value->items()[1].real().value_or(0)};
}

double numberOf(const document::Value* value) {
    return value != nullptr ? value->real().value_or(0) : 0;
}

} // namespace

ViewMotion motionOf(const document::Value& clicked) {
    return ViewMotion{.wheel = numberOf(clicked.find("wheel")),
                      .orbit = pairOf(clicked.find("orbit")),
                      .fly = numberOf(clicked.find("fly")),
                      .look = pairOf(clicked.find("look"))};
}

ViewMotion motionSince(const ViewMotion& now, const ViewMotion& read) noexcept {
    return ViewMotion{.wheel = now.wheel - read.wheel,
                      .orbit = {now.orbit[0] - read.orbit[0], now.orbit[1] - read.orbit[1]},
                      .fly = now.fly - read.fly,
                      .look = {now.look[0] - read.look[0], now.look[1] - read.look[1]}};
}

authoring::SceneView movedBy(const authoring::SceneView& view, const ViewMotion& change) noexcept {
    return flownView(
        lookedView(movedView(view, change.wheel, change.orbit[0], change.orbit[1]), change.look[0], change.look[1]),
        change.fly);
}

} // namespace rawframe::authoring_session
