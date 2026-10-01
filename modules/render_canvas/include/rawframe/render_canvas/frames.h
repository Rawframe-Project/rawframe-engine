#pragma once

// The canvas's frames as a device draws them (D279): what the canvas
// participant queued in this Host iteration's `present`, the view's size,
// and the textures its draws name, for the GPU half to record after it.

#include "rawframe/composition/participant.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/texture/texture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace rawframe::render_canvas {

/// A local player's region of the window in split-screen (ADR-0052, D364),
/// in pixels from its top left, and the frame queued for it; none while
/// its client has no World.
struct CanvasRegion {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const CanvasFrame* frame = nullptr;
};

class CanvasFrames {
public:
    CanvasFrames() = default;
    CanvasFrames(const CanvasFrames&) = delete;
    CanvasFrames& operator=(const CanvasFrames&) = delete;
    virtual ~CanvasFrames() = default;

    /// The frame queued in this Host iteration's `present`, until the next
    /// iteration's `presentation_extract`; none in an iteration that queued
    /// none, and while the canvas is idle.
    [[nodiscard]] virtual const CanvasFrame* queued() const noexcept = 0;
    /// In split-screen (D364), each local player's region and frame, the
    /// first's the frame `queued` gives, in the players' order; none while
    /// one player fills the window. Kept as `queued` is.
    [[nodiscard]] virtual std::span<const CanvasRegion> regionFrames() const noexcept = 0;
    /// The color around the regions, 8-bit sRGB: a constrained aspect's
    /// bars (D369), else black.
    [[nodiscard]] virtual std::array<std::uint8_t, 3> bars() const noexcept = 0;
    /// The view's size in pixels: `canvas.width` and `canvas.height` until
    /// what shows it sets another.
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    /// The view's size from the next frame on, as the window it is shown in
    /// has it (D281): the camera's aspect follows. Sides of nought are
    /// ignored.
    virtual void resize(std::uint32_t width, std::uint32_t height) noexcept = 0;
    /// The texture the game names `id`, decoded and held, for the frame
    /// queued; none while it is not ready.
    [[nodiscard]] virtual std::shared_ptr<const texture::Texture> texture(std::uint64_t id) const = 0;
};

inline constexpr composition::Capability<CanvasFrames> kCanvasFrames{"rawframe.render_canvas.frames"};

} // namespace rawframe::render_canvas
