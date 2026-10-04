#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::process {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kProcessDomain{base::parseBits128Hex("cc2a2ccb995e285433579379ce6897b2").value};

enum class ProcessError : std::uint32_t {
    /// The system would not start the program: no such file, not
    /// executable, or no room for another process.
    StartFailed = 1,
    /// The child could not be told to stop: it has ended, or the system
    /// refused.
    StopFailed = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(ProcessError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::process
