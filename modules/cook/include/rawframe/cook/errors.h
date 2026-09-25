#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::cook {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kCookDomain{base::parseBits128Hex("d4080b7ac52f5d390f5cfd9d6fe0667e").value};

/// Codes within kCookDomain.
enum class CookError : std::uint32_t {
    /// The request itself: sources unreadable, output inside the sources.
    BadRequest = 1,
    /// A sidecar that is not one: its schema, fields, or identity.
    BadSidecar = 2,
    /// A sidecar's source is missing or unreadable.
    MissingSource = 3,
    /// A sidecar names no registered importer.
    UnknownImporter = 4,
    /// Two sidecars claim one resource.
    DuplicateResource = 5,
    /// Two cooks of the same inputs gave different bytes.
    Nondeterministic = 6,
    /// Writing the output failed.
    WriteFailed = 7,
    /// An importer's read: outside the sources, or of what cannot be read.
    BadRead = 8,
    /// A name a source uses that resolves to nothing, or to what does not
    /// do: a program that does not compile, a document that does not read.
    BadReference = 9,
};

[[nodiscard]] constexpr result::ErrorCode code(CookError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::cook
