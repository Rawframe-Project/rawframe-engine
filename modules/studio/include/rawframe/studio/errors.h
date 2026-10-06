#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::studio {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kStudioDomain{base::parseBits128Hex("918a45bdd8db2f591e2056c8157ccfdc").value};

enum class StudioError : std::uint32_t {
    /// A game to preview could not be played: its directory or files could
    /// not be written (D445).
    PlayFailed = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(StudioError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::studio
