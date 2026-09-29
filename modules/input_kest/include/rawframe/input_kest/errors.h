#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::input_kest {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kInputKestDomain{base::parseBits128Hex("e67582833ca7e5d78164bc2d0d49d9a6").value};

/// Codes within kInputKestDomain.
enum class InputKestError : std::uint32_t {
    /// The game declares no `actions` and `sample` lines.
    NoControls = 1,
    /// The sample program does not fit the game: no such entry, or its input
    /// type is not the size the game's input component is.
    BadSample = 2,
    /// The sample function refused, or ran out of fuel.
    SampleFailed = 3,
    /// The host lends no devices, or their player already has a source.
    NoDevices = 4,
    /// An effect is felt by a haptic output the game's action set does not
    /// declare (D251).
    UnknownHaptic = 5,
};

[[nodiscard]] constexpr result::ErrorCode code(InputKestError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::input_kest
