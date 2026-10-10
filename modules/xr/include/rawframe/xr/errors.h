#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::xr {

/// The domain of every Error this module creates (ADR-0081, D591).
inline constexpr result::ErrorDomain kXrDomain{base::parseBits128Hex("32574ba22afcb1c6fe3239ba37b4fa1b").value};

enum class XrError : std::uint32_t {
    /// No OpenXR runtime answered, or its runtime has no head-mounted
    /// system now (none installed, its service not running, the headset
    /// off).
    NoRuntime = 1,
    /// The runtime refused or failed: an instance, a session, or a frame.
    Runtime = 2,
    /// Asked at the wrong time: a session on a device not ready, or an
    /// exit asked of a session not running.
    State = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(XrError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::xr
