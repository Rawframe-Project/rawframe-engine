#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::release {

/// The domain of every Error this module creates: SPEC-0020's typed
/// rejections of release records, channel pointers, and update checks.
inline constexpr result::ErrorDomain kReleaseDomain{base::parseBits128Hex("740a12f872c52b3e43194839df8a7930").value};

enum class ReleaseError : std::uint32_t {
    /// Not the record's canonical bytes, an unknown or missing member, or a
    /// value outside its grammar or bounds.
    RecordInvalid = 1,
    /// A pointer names another Release than the record given.
    DigestMismatch = 2,
    /// A pointer whose sequence is not greater than the one held: a replay.
    SequenceRegression = 3,
    /// A pointer or record of another subject or channel than asked for.
    UnknownSubject = 4,
    /// An artifact's bytes are not its size.
    SizeMismatch = 5,
    /// The Release has no artifact of the kind asked for.
    NoArtifact = 6,
};

[[nodiscard]] constexpr result::ErrorCode code(ReleaseError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::release
