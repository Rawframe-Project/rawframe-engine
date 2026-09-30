#pragma once

// The canvas's frames as a device draws them (D279): what the canvas
// participant queued in this Host iteration's `present`, the view's size,
// and the textures its draws name, for the GPU half to record after it.

#include "rawframe/composition/participant.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/texture/texture.h"

#include <cstdint>
#include <memory>

namespace rawframe::render_canvas {

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
    /// The view's size in pixels (`canvas.width`, `canvas.height`).
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    /// The texture the game names `id`, decoded and held, for the frame
    /// queued; none while it is not ready.
    [[nodiscard]] virtual std::shared_ptr<const texture::Texture> texture(std::uint64_t id) const = 0;
};

inline constexpr composition::Capability<CanvasFrames> kCanvasFrames{"rawframe.render_canvas.frames"};

} // namespace rawframe::render_canvas
