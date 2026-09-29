#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::render {

/// The domain of every Error this module creates (SPEC-0024).
inline constexpr result::ErrorDomain kRenderDomain{base::parseBits128Hex("6015492075ca36b9ddd79cd27d007fa5").value};

enum class RenderError : std::uint32_t {
    /// No adapter answered that the settings allow: no GPU, no software
    /// rasterizer, or no loader.
    NoAdapter = 1,
    /// The device layer refused or failed: an adapter, a device, or work on
    /// it.
    Device = 2,
    /// A named limit was reached.
    OverLimit = 3,
    /// Asked at the wrong time: of a device still opening, or one that
    /// failed.
    State = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(RenderError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::render
