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
    /// a point that is not finite.
    Invalid = 1,
    /// A named limit was reached: the tiles, a tile's triangles, polygons,
    /// or links, the memory, or a search's nodes.
    Limit = 2,
    /// Memory ran out.
    Capacity = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(NavigationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::navigation
