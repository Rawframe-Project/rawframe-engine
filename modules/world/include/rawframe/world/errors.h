#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldDomain{base::parseBits128Hex("32a71b5db3a1f5b9f868a55a80e1227e").value};

/// Codes within kWorldDomain.
enum class WorldError : std::uint32_t {
    StaleEntity = 1,
    EntityCapacity = 2,
    StructureLocked = 3,
    CommandCapacity = 4,
    DuplicateSystem = 5,
    UnknownSystem = 6,
    CrossPhaseOrdering = 7,
    SystemCycle = 8,
    UndeclaredAccess = 9,
    InvalidTickRate = 10,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world
