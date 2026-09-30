#pragma once

// A reflection probe's picture baked (D326): the scene drawn six ways from
// the probe's middle, each way's light read back, and the six resampled
// into the equirectangular picture of all directions the cook makes an
// environment of (D321). Toolchain only: no process that plays reaches it.

#include "rawframe/render_scene/scene.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/texture_import/import.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace rawframe::scene_bake {

/// The six cameras a probe is baked through, from `eye`: along +X, -X,
/// +Y, -Y, +Z, and -Z, each seeing a little past a quarter turn both ways
/// at `aspect`, so the six overlap; at `exposure` (EV100), unmetered.
[[nodiscard]] std::array<render_scene::SceneCamera, 6>
faceCameras(const std::array<double, 3>& eye, float aspect, float exposure) noexcept;

/// One way the scene was seen: the view and projection it was drawn
/// through, and its light.
struct BakedFace {
    render_scene::Matrix view{};
    render_scene::Matrix projection{};
    render_scene_gpu::LightCapture light;
};

/// The equirectangular picture, `width` by half of it, of what `faces`
/// saw: each direction taken from the face it is most ahead of, bilinear
/// between its pixels. None without a face whose light fills its sides.
[[nodiscard]] std::optional<texture_import::LightImage> pictureOf(std::span<const BakedFace> faces,
                                                                  std::uint32_t width);

} // namespace rawframe::scene_bake
