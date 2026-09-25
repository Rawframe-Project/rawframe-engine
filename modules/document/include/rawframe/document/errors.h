#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::document {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kDocumentDomain{base::parseBits128Hex("28a1802e1ab9822e7527ddd759ca0075").value};

/// Codes within kDocumentDomain. A failure while reading text says where, as
/// `line` and `column` context, both counted from one.
enum class DocumentError : std::uint32_t {
    /// Not JSON, or JSON the authored-document profile refuses: a byte order
    /// mark, a duplicate key, a number no double holds, text that is not
    /// UTF-8.
    Malformed = 1,
    /// Well formed, but not written the one way the profile writes it.
    NotCanonical = 2,
    /// Deeper or longer than the reader was allowed.
    TooLarge = 3,
    /// A document of the right shape in JSON but not of its kind: a field
    /// missing, of the wrong type, or out of range. For readers of typed
    /// documents built on this module.
    Invalid = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(DocumentError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::document
