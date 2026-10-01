#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::render_canvas {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kRenderCanvasDomain{
    base::parseBits128Hex("fa22fa42d208b9cf70a54fbf1d8e295b").value};

/// Codes within kRenderCanvasDomain.
enum class RenderCanvasError : std::uint32_t {
    /// The game declares no component of `rawframe.canvas.Sprite`'s type.
    NoSprites = 1,
    /// Its sprite component is not laid out as `rawframe.canvas` lays it
    /// out, or is not the size this module reads.
    BadComponents = 2,
    /// A canvas material's read was cancelled (D356).
    MaterialUnreadable = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(RenderCanvasError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::render_canvas
