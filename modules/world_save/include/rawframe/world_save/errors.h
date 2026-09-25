#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_save {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kSaveDomain{base::parseBits128Hex("2175e28dc09d97b1671fcef8fb459532").value};

/// Codes within kSaveDomain (ADR-0057's typed outcomes).
enum class SaveError : std::uint32_t {
    /// Bytes that are not a generation-1 save: a wrong magic, a length that
    /// runs past the end, entities out of order, or bytes after the digest.
    Malformed = 1,
    /// A digest that does not match its bytes.
    DigestMismatch = 2,
    /// A save of a later format than this engine reads.
    TooNew = 3,
    /// A save of another persistence namespace, document, or declaration.
    Mismatch = 4,
    /// More entities or bytes than the limits allow.
    LimitExceeded = 5,
    /// A saved value names an entity that has no persistent identity.
    UnnamedReference = 6,
    /// A save names an identity neither it nor the World holds.
    UnknownReference = 7,
    /// Two entities of the World hold one identity.
    DuplicateIdentity = 8,
    /// A declaration naming a component the World's registry lacks, one
    /// twice, more than 64, or an entity field outside a value.
    InvalidDeclaration = 9,
    /// No save is kept under the slot asked for.
    Absent = 10,
    /// The store could not read or keep a save; what it kept before is
    /// untouched.
    StorageFailed = 11,
    /// A slot name that is not 1 to 64 of a-z, 0-9, `_`, and `-`.
    InvalidSlot = 12,
};

[[nodiscard]] constexpr result::ErrorCode code(SaveError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_save
