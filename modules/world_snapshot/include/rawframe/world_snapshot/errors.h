#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_snapshot {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kSnapshotDomain{base::parseBits128Hex("64aabcdd2fbd0923cb3bbdb0ec48a1f2").value};

/// Codes within kSnapshotDomain (SPEC-0011 failure distinctions).
enum class SnapshotError : std::uint32_t {
    /// Bytes that are not a generation-1 checkpoint: a wrong magic, size,
    /// order, reserved value, or a value outside its field.
    Malformed = 1,
    /// A stored digest that does not match its bytes.
    DigestMismatch = 2,
    /// No completion footer at the end: the artifact was never finished.
    Incomplete = 3,
    /// A checkpoint of another game, schema, projection, codec, or profile.
    Mismatch = 4,
    /// More entities, rows, or bytes than the limit profile allows.
    LimitExceeded = 5,
    /// An entity reference to nothing the checkpoint holds.
    BadReference = 6,
    /// A candidate World that is not empty, or a projection that does not
    /// fit the World's registry.
    InvalidCandidate = 7,
};

[[nodiscard]] constexpr result::ErrorCode code(SnapshotError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_snapshot
