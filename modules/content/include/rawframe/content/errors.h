#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::content {

/// The domain of every Error this module creates (SPEC-0008's content error
/// domain). A manifest the document profile refuses is refused in
/// `rawframe.document`'s domain.
inline constexpr result::ErrorDomain kContentDomain{base::parseBits128Hex("3131d7054967dc7f6731a2284291560b").value};

/// SPEC-0008's stable codes within kContentDomain.
enum class ContentError : std::uint32_t {
    ManifestInvalid = 1,
    UnsupportedManifestVersion = 2,
    InvalidResourceId = 3,
    InvalidResourceType = 4,
    UnsupportedRepresentation = 5,
    InvalidLocator = 6,
    PathEscape = 7,
    DuplicateResource = 8,
    SourceUnavailable = 9,
    ResourceNotFound = 10,
    ResourceTypeMismatch = 11,
    RevisionMismatch = 12,
    ResourceTooLarge = 13,
    QuotaExhausted = 14,
    ReadFailed = 15,
    ShortRead = 16,
    SourceChanged = 17,
    DigestMismatch = 18,
    Cancelled = 19,
    DeadlineExceeded = 20,
};

[[nodiscard]] constexpr result::ErrorCode code(ContentError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::content
