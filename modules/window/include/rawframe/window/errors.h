#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::window {

/// The domain of every Error this module creates (SPEC-0025).
inline constexpr result::ErrorDomain kWindowDomain{base::parseBits128Hex("6d278535781b04ae97ab405f8a845fab").value};

enum class WindowError : std::uint32_t {
    /// A window, gamepad, or request that no longer exists.
    Stale = 1,
    /// A named limit was reached: windows, requests in flight, or a title
    /// past its bytes.
    OverLimit = 2,
    /// This platform or this build cannot do it: a backend it lacks.
    Unsupported = 3,
    /// The window system could not be reached or failed.
    Platform = 4,
    /// An argument outside its contract: text that is not UTF-8, a size
    /// that is not positive.
    Invalid = 5,
    /// Asked at the wrong time: from inside the running program's own
    /// loop, or of a window not yet created.
    State = 6,
};

[[nodiscard]] constexpr result::ErrorCode code(WindowError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::window
