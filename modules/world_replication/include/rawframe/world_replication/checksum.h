#pragma once

// SPEC-0041's pinned checksum over a player's whole predicted scope (D203):
// what a client hashes at a confirmed tick and a server at the same tick, so
// a value a state never carried, because it had not changed, is compared
// too. The algorithm's identity is in its domain tags; changing it is a new
// identity, and golden vectors hold this one.

#include "rawframe/schema/stable_id.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::world_replication {

/// The first eight bytes of SHA-256, little-endian, over
/// `rawframe.prediction.scope.v1` and each predicted component's identity
/// (its 128 bits, most significant first) in `predicted` order: which scope
/// a checksum is of, so a client of another game or declaration is told
/// apart from one that diverged.
[[nodiscard]] std::uint64_t scopeFingerprint(std::span<const schema::ComponentTypeId> predicted) noexcept;

/// The first eight bytes of SHA-256, little-endian, over
/// `rawframe.prediction.checksum.v1` and, for each predicted component in
/// order, its value's length as four bytes little-endian and then the value
/// in its codec's wire form: the canonical encoding both sides share.
[[nodiscard]] std::uint64_t predictedChecksum(std::span<const std::span<const std::byte>> encoded) noexcept;

} // namespace rawframe::world_replication
