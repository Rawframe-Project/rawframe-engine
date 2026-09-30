#include "rawframe/scene_bake/bake.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rawframe::scene_bake {

namespace {

/// A quarter turn and a little: the faces overlap, so a direction on the
/// edge between two is seen whole by the one it is most ahead of.
constexpr float kQuarterAndMore = (std::numbers::pi_v<float> / 2) * 1.1F;

/// `matrix` times a direction (w nought), x, y, and w; column-major.
std::array<double, 3> directed(const render_scene::Matrix& matrix, const std::array<double, 3>& direction) noexcept {
    std::array<double, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            out[row] += static_cast<double>(matrix[(column * 4) + row]) * direction[column];
        }
    }
    return {out[0], out[1], out[3]};
}

render_scene::Matrix product(const render_scene::Matrix& left, const render_scene::Matrix& right) noexcept {
    render_scene::Matrix made{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0;
            for (std::size_t inner = 0; inner < 4; ++inner) {
                sum += left[(inner * 4) + row] * right[(column * 4) + inner];
            }
            made[(column * 4) + row] = sum;
        }
    }
    return made;
}

} // namespace

std::array<render_scene::SceneCamera, 6>
faceCameras(const std::array<double, 3>& eye, float aspect, float exposure) noexcept {
    constexpr float kQuarter = std::numbers::pi_v<float> / 2;
    // Yaw nought looks along -Z and a quarter turn less along +X; pitch
    // looks up.
    constexpr std::array<std::array<float, 2>, 6> kWays = {
        {{-kQuarter, 0}, {kQuarter, 0}, {0, kQuarter}, {0, -kQuarter}, {std::numbers::pi_v<float>, 0}, {0, 0}}};
    // Tall or wide, neither side sees less than a quarter turn and more.
    const float kAspect = aspect > 0 && std::isfinite(aspect) ? aspect : 1.0F;
    const float kFovY = kAspect >= 1 ? kQuarterAndMore : 2 * std::atan(std::tan(kQuarterAndMore / 2) / kAspect);
    std::array<render_scene::SceneCamera, 6> made{};
    for (std::size_t face = 0; face < 6; ++face) {
        made[face] = render_scene::SceneCamera{.eye = eye,
                                               .yaw = kWays[face][0],
                                               .pitch = kWays[face][1],
                                               .fovY = kFovY,
                                               .near = 0.05F,
                                               .exposure = exposure,
                                               .aspect = kAspect};
    }
    return made;
}

std::optional<texture_import::LightImage> pictureOf(std::span<const BakedFace> faces, std::uint32_t width) {
    std::vector<const BakedFace*> usable;
    std::vector<render_scene::Matrix> seen;
    for (const BakedFace& face : faces) {
        if (face.light.width > 0 && face.light.height > 0 &&
            face.light.light.size() == std::size_t{face.light.width} * face.light.height * 3) {
            usable.push_back(&face);
            seen.push_back(product(face.projection, face.view));
        }
    }
    if (usable.empty() || width < 2) {
        return std::nullopt;
    }
    texture_import::LightImage picture{.width = width, .height = width / 2};
    picture.rgb.reserve(std::size_t{picture.width} * picture.height * 3);
    for (std::uint32_t row = 0; row < picture.height; ++row) {
        for (std::uint32_t x = 0; x < picture.width; ++x) {
            const std::array<double, 3> kWay =
                texture_import::pictureDirection((x + 0.5) / picture.width, (row + 0.5) / picture.height);
            // The face the direction is most ahead of.
            std::size_t best = 0;
            std::array<double, 3> bestClip{};
            for (std::size_t at = 0; at < usable.size(); ++at) {
                const std::array<double, 3> kClip = directed(seen[at], kWay);
                if (at == 0 || kClip[2] > bestClip[2]) {
                    best = at;
                    bestClip = kClip;
                }
            }
            const render_scene_gpu::LightCapture& kLight = usable[best]->light;
            // Where it is on that face's target, rows top first.
            const double kW = std::max(bestClip[2], 1e-9);
            const double kX = std::clamp(
                (((bestClip[0] / kW) + 1) / 2 * kLight.width) - 0.5, 0.0, static_cast<double>(kLight.width - 1));
            const double kY = std::clamp(
                (((1 - (bestClip[1] / kW)) / 2) * kLight.height) - 0.5, 0.0, static_cast<double>(kLight.height - 1));
            const auto kLeft = static_cast<std::uint32_t>(kX);
            const auto kTop = static_cast<std::uint32_t>(kY);
            const std::uint32_t kRight = std::min(kLeft + 1, kLight.width - 1);
            const std::uint32_t kBottom = std::min(kTop + 1, kLight.height - 1);
            const double kAcross = kX - kLeft;
            const double kDown = kY - kTop;
            const auto kAt = [&kLight](std::uint32_t column, std::uint32_t line, std::size_t channel) {
                return static_cast<double>(kLight.light[((std::size_t{line} * kLight.width + column) * 3) + channel]);
            };
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double kUpper =
                    (kAt(kLeft, kTop, channel) * (1 - kAcross)) + (kAt(kRight, kTop, channel) * kAcross);
                const double kLower =
                    (kAt(kLeft, kBottom, channel) * (1 - kAcross)) + (kAt(kRight, kBottom, channel) * kAcross);
                picture.rgb.push_back(static_cast<float>((kUpper * (1 - kDown)) + (kLower * kDown)));
            }
        }
    }
    return picture;
}

} // namespace rawframe::scene_bake
