#include "camera.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

SceneMetering meteringOf(const SceneCamera& camera) noexcept {
    if (!camera.metering.has_value()) {
        return {};
    }
    AutoExposure asked = *camera.metering;
    if (!std::ranges::all_of(std::array{asked.minimum,
                                        asked.maximum,
                                        asked.brighten,
                                        asked.darken,
                                        asked.compensation,
                                        asked.low,
                                        asked.high,
                                        asked.centered},
                             [](float value) {
                                 return std::isfinite(value);
                             })) {
        return {};
    }
    if (asked.maximum <= asked.minimum) {
        return {};
    }
    asked.brighten = std::max(asked.brighten, 0.0F);
    asked.darken = std::max(asked.darken, 0.0F);
    asked.low = std::clamp(asked.low, 0.0F, 1.0F);
    asked.high = std::clamp(asked.high, asked.low, 1.0F);
    asked.centered = std::clamp(asked.centered, 0.0F, 1.0F);
    const float kElapsed = std::isfinite(camera.elapsed) ? std::clamp(camera.elapsed, 0.0F, 0.25F) : 0.0F;
    return {.enabled = true, .settings = asked, .elapsed = kElapsed};
}

Matrix times(const Matrix& left, const Matrix& right) noexcept {
    Matrix out{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += left[(k * 4) + row] * right[(column * 4) + k];
            }
            out[(column * 4) + row] = sum;
        }
    }
    return out;
}

std::array<Vector, 3> axesOf(float yaw, float pitch) noexcept {
    const view::Axes kAxes = view::axesOf(yaw, pitch);
    return {kAxes.right, kAxes.up, kAxes.forward};
}

bool orientationSound(const SceneCamera& camera) noexcept {
    if (!camera.orientation.has_value()) {
        return true;
    }
    const auto& [kX, kY, kZ, kW] = *camera.orientation;
    const float kLength = (kX * kX) + (kY * kY) + (kZ * kZ) + (kW * kW);
    return std::isfinite(kLength) && std::abs(kLength - 1) < 1e-3F;
}

std::array<Vector, 3> axesOf(const SceneCamera& camera) noexcept {
    if (!camera.orientation.has_value()) {
        return axesOf(camera.yaw, camera.pitch);
    }
    // The quaternion's turn of the eye's own right, up, and forward (-Z).
    const auto& [kX, kY, kZ, kW] = *camera.orientation;
    const Vector kRight{1 - (2 * ((kY * kY) + (kZ * kZ))), 2 * ((kX * kY) + (kZ * kW)), 2 * ((kX * kZ) - (kY * kW))};
    const Vector kUp{2 * ((kX * kY) - (kZ * kW)), 1 - (2 * ((kX * kX) + (kZ * kZ))), 2 * ((kY * kZ) + (kX * kW))};
    const Vector kBack{2 * ((kX * kZ) + (kY * kW)), 2 * ((kY * kZ) - (kX * kW)), 1 - (2 * ((kX * kX) + (kY * kY)))};
    return {kRight, kUp, Vector{-kBack[0], -kBack[1], -kBack[2]}};
}

CameraMatrices matricesOf(const SceneCamera& camera) noexcept {
    const auto [kRight, kUp, kForward] = axesOf(camera);
    const float kFocal = 1 / std::tan(camera.fovY / 2);
    // Reversed-Z with the far plane at infinity (ADR-0051): clip z is the
    // near distance and w the distance ahead, so depth is one at the near
    // plane and falls toward nought.
    return CameraMatrices{.view = Matrix{kRight[0],
                                         kUp[0],
                                         -kForward[0],
                                         0,
                                         kRight[1],
                                         kUp[1],
                                         -kForward[1],
                                         0,
                                         kRight[2],
                                         kUp[2],
                                         -kForward[2],
                                         0,
                                         0,
                                         0,
                                         0,
                                         1},
                          .projection = Matrix{camera.aspect == 0 ? 0 : kFocal / camera.aspect,
                                               0,
                                               0,
                                               0,
                                               0,
                                               kFocal,
                                               0,
                                               0,
                                               0,
                                               0,
                                               0,
                                               -1,
                                               0,
                                               0,
                                               camera.near,
                                               0}};
}

namespace {

/// The widest side an eye's picture is drawn at.
constexpr float kWidestEye = 4096;

/// `left` turned by `right` after it: quaternions, x, y, z, w.
std::array<float, 4> multiplied(const std::array<float, 4>& left, const std::array<float, 4>& right) noexcept {
    const auto& [kX, kY, kZ, kW] = left;
    const auto& [kU, kV, kS, kT] = right;
    return {(kW * kU) + (kX * kT) + (kY * kS) - (kZ * kV),
            (kW * kV) - (kX * kS) + (kY * kT) + (kZ * kU),
            (kW * kS) + (kX * kV) - (kY * kU) + (kZ * kT),
            (kW * kT) - (kX * kU) - (kY * kV) - (kZ * kS)};
}

} // namespace

std::optional<EyeView> eyeViewOf(const SceneCamera& player, const view::HeadsetEye& eye) {
    const float kLeft = std::tan(eye.angleLeft);
    const float kRight = std::tan(eye.angleRight);
    const float kUp = std::tan(eye.angleUp);
    const float kDown = std::tan(eye.angleDown);
    const float kAcross = std::max(-kLeft, kRight);
    const float kHigh = std::max(kUp, -kDown);
    if (!(kRight > kLeft && kUp > kDown && kAcross > 0 && kHigh > 0 && std::isfinite(kAcross) && std::isfinite(kHigh) &&
          eye.width != 0 && eye.height != 0 && std::isfinite(player.yaw))) {
        return std::nullopt;
    }
    const auto kWidth = static_cast<float>(eye.width);
    const auto kHeight = static_cast<float>(eye.height);
    const float kWide = 2 * kAcross / (kRight - kLeft) * kWidth;
    const float kTall = 2 * kHigh / (kUp - kDown) * kHeight;
    EyeView view{.camera = player};
    // The headset's place about the play space's origin, turned by its
    // heading.
    const auto& [kX, kY, kZ] = eye.position;
    const float kCos = std::cos(player.yaw);
    const float kSin = std::sin(player.yaw);
    view.camera.eye = {
        player.eye[0] + (kCos * kX) + (kSin * kZ), player.eye[1] + kY, player.eye[2] - (kSin * kX) + (kCos * kZ)};
    const std::array<float, 4> kHeading{0, std::sin(player.yaw / 2), 0, std::cos(player.yaw / 2)};
    view.camera.orientation = multiplied(kHeading, eye.orientation);
    view.camera.fovY = 2 * std::atan(kHigh);
    view.camera.aspect = kAcross / kHigh;
    view.width = static_cast<std::uint32_t>(std::clamp(std::ceil(kWide), 1.0F, kWidestEye));
    view.height = static_cast<std::uint32_t>(std::clamp(std::ceil(kTall), 1.0F, kWidestEye));
    view.viewport = {
        (-kAcross - kLeft) / (kRight - kLeft) * kWidth, (kUp - kHigh) / (kUp - kDown) * kHeight, kWide, kTall};
    return view;
}

std::optional<std::array<double, 3>> eyeOf(const Camera& camera, const physics3d::Pose3D* pose) noexcept {
    if (camera.anchor == kCameraFixed) {
        return std::array<double, 3>{camera.offsetX, camera.offsetY, camera.offsetZ};
    }
    if (pose == nullptr) {
        return std::nullopt;
    }
    return std::array<double, 3>{pose->x + camera.offsetX, pose->y + camera.offsetY, pose->z + camera.offsetZ};
}

view::Perspective perspectiveOf(const SceneCamera& camera) noexcept {
    return view::Perspective{
        .eye = camera.eye, .yaw = camera.yaw, .pitch = camera.pitch, .fovY = camera.fovY, .near = camera.near};
}

} // namespace rawframe::render_scene
