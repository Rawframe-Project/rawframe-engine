#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::navigation {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kNavigationDomain{base::parseBits128Hex("485598761a6c8a8ba2fcf0dc74eb740e").value};

enum class NavigationError : std::uint32_t {
    /// A setting, an input, or a query is out of range: a setting Maul Nav
    /// refuses, geometry that is not finite or past what a navmesh holds,
    /// a point that is not finite. More room would not help.
    Invalid = 1,
    /// Room ran out: the tiles, the memory or a search's nodes a navmesh
    /// was made with, the agents avoidance holds, or memory itself. More
    /// room would succeed.
    Limit = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(NavigationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::navigation
