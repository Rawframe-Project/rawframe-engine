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

CameraMatrices matricesOf(const SceneCamera& camera) noexcept {
    const auto [kRight, kUp, kForward] = axesOf(camera.yaw, camera.pitch);
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
