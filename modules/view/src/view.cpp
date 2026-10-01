#include "rawframe/view/view.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawframe::view {

namespace {

using Vector = std::array<float, 3>;

Vector cross(const Vector& a, const Vector& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

Vector normalized(const Vector& v) noexcept {
    const float kLength = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
    return kLength > 0 ? Vector{v[0] / kLength, v[1] / kLength, v[2] / kLength} : Vector{0, 0, 0};
}

bool sized(ViewSize size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height) && size.width > 0 && size.height > 0;
}

/// A perspective view's failure before any point, if it has one.
std::expected<void, Failure> checked(const Perspective& view, ViewSize size) noexcept {
    if (!sized(size)) {
        return std::unexpected{Failure::NoViewSize};
    }
    const bool kFinite = std::ranges::all_of(view.eye,
                                             [](double value) {
                                                 return std::isfinite(value);
                                             }) &&
                         std::isfinite(view.yaw) && std::isfinite(view.pitch) && std::isfinite(view.fovY) &&
                         std::isfinite(view.near);
    if (!kFinite) {
        return std::unexpected{Failure::NotFinite};
    }
    if (view.fovY <= 0 || view.fovY >= std::numbers::pi_v<float> || view.near <= 0) {
        return std::unexpected{Failure::SeesNothing};
    }
    return {};
}

std::expected<void, Failure> checked(const Orthographic& view, ViewSize size) noexcept {
    if (!sized(size)) {
        return std::unexpected{Failure::NoViewSize};
    }
    if (!std::isfinite(view.middle[0]) || !std::isfinite(view.middle[1]) || !std::isfinite(view.height)) {
        return std::unexpected{Failure::NotFinite};
    }
    if (view.height <= 0) {
        return std::unexpected{Failure::SeesNothing};
    }
    return {};
}

} // namespace

Axes axesOf(float yaw, float pitch) noexcept {
    constexpr float kSteepest = (std::numbers::pi_v<float> / 2) - 0.001F;
    const float kPitch = std::clamp(pitch, -kSteepest, kSteepest);
    const Vector kForward = {-std::sin(yaw) * std::cos(kPitch), std::sin(kPitch), -std::cos(yaw) * std::cos(kPitch)};
    const Vector kRight = normalized(cross(kForward, {0, 1, 0}));
    return {.right = kRight, .up = cross(kRight, kForward), .forward = kForward};
}

std::expected<Ray, Failure> pointToRay(const Perspective& view, ViewSize size, float x, float y) noexcept {
    if (auto kChecked = checked(view, size); !kChecked.has_value()) {
        return std::unexpected{kChecked.error()};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return std::unexpected{Failure::NotFinite};
    }
    const auto [kRight, kUp, kForward] = axesOf(view.yaw, view.pitch);
    const float kSlope = std::tan(view.fovY / 2);
    // The point on the plane one meter ahead, in the eye's axes.
    const float kAcross = ((2 * x / size.width) - 1) * kSlope * (size.width / size.height);
    const float kUpward = (1 - (2 * y / size.height)) * kSlope;
    Vector ahead{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        ahead[axis] = (kRight[axis] * kAcross) + (kUp[axis] * kUpward) + kForward[axis];
    }
    Ray ray{.direction = normalized(ahead)};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        ray.origin[axis] = view.eye[axis] + (static_cast<double>(ahead[axis]) * view.near);
    }
    return ray;
}

std::expected<ViewPoint, Failure>
worldToPoint(const Perspective& view, ViewSize size, const std::array<double, 3>& world) noexcept {
    if (auto kChecked = checked(view, size); !kChecked.has_value()) {
        return std::unexpected{kChecked.error()};
    }
    if (!std::ranges::all_of(world, [](double value) {
            return std::isfinite(value);
        })) {
        return std::unexpected{Failure::NotFinite};
    }
    const auto [kRight, kUp, kForward] = axesOf(view.yaw, view.pitch);
    // Relative to the eye in doubles first, as the scene draws (ADR-0046).
    const Vector kFrom = {static_cast<float>(world[0] - view.eye[0]),
                          static_cast<float>(world[1] - view.eye[1]),
                          static_cast<float>(world[2] - view.eye[2])};
    const auto kDot = [&kFrom](const Vector& axis) {
        return (kFrom[0] * axis[0]) + (kFrom[1] * axis[1]) + (kFrom[2] * axis[2]);
    };
    const float kDepth = kDot(kForward);
    if (kDepth <= view.near) {
        return std::unexpected{Failure::BehindNear};
    }
    const float kSlope = std::tan(view.fovY / 2);
    const float kAcross = kDot(kRight) / (kDepth * kSlope * (size.width / size.height));
    const float kUpward = kDot(kUp) / (kDepth * kSlope);
    return ViewPoint{.x = (kAcross + 1) / 2 * size.width, .y = (1 - kUpward) / 2 * size.height, .depth = kDepth};
}

std::expected<std::array<double, 2>, Failure>
pointToWorld(const Orthographic& view, ViewSize size, float x, float y) noexcept {
    if (auto kChecked = checked(view, size); !kChecked.has_value()) {
        return std::unexpected{kChecked.error()};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return std::unexpected{Failure::NotFinite};
    }
    const double kHalfHeight = view.height / 2.0;
    const double kHalfWidth = kHalfHeight * size.width / size.height;
    return std::array<double, 2>{view.middle[0] + (((2.0 * x / size.width) - 1) * kHalfWidth),
                                 view.middle[1] + ((1 - (2.0 * y / size.height)) * kHalfHeight)};
}

std::expected<ViewPoint, Failure>
worldToPoint(const Orthographic& view, ViewSize size, const std::array<double, 2>& world) noexcept {
    if (auto kChecked = checked(view, size); !kChecked.has_value()) {
        return std::unexpected{kChecked.error()};
    }
    if (!std::isfinite(world[0]) || !std::isfinite(world[1])) {
        return std::unexpected{Failure::NotFinite};
    }
    const double kHalfHeight = view.height / 2.0;
    const double kHalfWidth = kHalfHeight * size.width / size.height;
    return ViewPoint{.x = static_cast<float>(((world[0] - view.middle[0]) / kHalfWidth + 1) / 2 * size.width),
                     .y = static_cast<float>((1 - ((world[1] - view.middle[1]) / kHalfHeight)) / 2 * size.height)};
}

} // namespace rawframe::view
