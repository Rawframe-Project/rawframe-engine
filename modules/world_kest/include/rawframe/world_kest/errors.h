#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_kest {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldKestDomain{base::parseBits128Hex("814e6e3519139dff9a23ba393e9a332a").value};

/// Codes within kWorldKestDomain.
enum class WorldKestError : std::uint32_t {
    /// A column's component is not plain data, or its size or alignment
    /// differs from the Kest type it is lent as.
    ColumnMismatch = 1,
    /// The entry's frame is not `count` plus one slot per data column.
    EntryMismatch = 2,
    /// The machine was cancelled while the system ran.
    Cancelled = 3,
    /// An archetype holds more rows than a Kest `i32` count can say.
    TooManyRows = 4,
    /// A game description line does not parse; the context names the line.
    BadGameLine = 5,
    /// A game description names a component or field it does not declare.
    UnknownName = 6,
    /// A game file could not be read.
    UnreadableFile = 7,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldKestError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_kest
