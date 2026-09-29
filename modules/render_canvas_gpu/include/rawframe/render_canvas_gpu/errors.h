#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::render_canvas_gpu {

/// The domain of every Error this module creates (SPEC-0024's canvas).
inline constexpr result::ErrorDomain kCanvasGpuDomain{base::parseBits128Hex("2079ada685169d5f121be63722eeb709").value};

enum class CanvasGpuError : std::uint32_t {
    /// The device refused or failed work: a shader, a pipeline, a texture,
    /// a frame.
    Device = 1,
    /// A named limit was passed: a target's sides.
    OverLimit = 2,
    /// Asked of a device that is not ready, or was lost.
    State = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(CanvasGpuError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::render_canvas_gpu
