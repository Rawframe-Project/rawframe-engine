#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_replication {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kReplicationDomain{
    base::parseBits128Hex("e4cdd27feea52f2d8110247211613d6b").value};

/// Codes within kReplicationDomain.
enum class ReplicationError : std::uint32_t {
    /// A replication payload whose fields are not acceptable.
    Malformed = 1,
    /// A component index outside the admitted replication table.
    UnknownComponent = 2,
    /// A component field this generation cannot put on the wire.
    FieldUnsupported = 3,
    /// A record names an entity with no live mapping in its epoch.
    MappingUnknown = 4,
    /// A connection's entity mapping reached its bound.
    MappingExhausted = 5,
    /// A record from another replication or input epoch.
    StaleEpoch = 6,
    /// An input window over its bound, or from too far ahead.
    InputRefused = 7,
};

[[nodiscard]] constexpr result::ErrorCode code(ReplicationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_replication
