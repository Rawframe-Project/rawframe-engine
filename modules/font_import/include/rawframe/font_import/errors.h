#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::font_import {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kFontImportDomain{base::parseBits128Hex("a2498c368944721a6d18ae34029af048").value};

/// Codes within kFontImportDomain.
enum class FontImportError : std::uint32_t {
    /// Bytes that are not a TrueType or OpenType font or collection, or one
    /// the sanitizer refuses.
    BadFont = 1,
    /// A web font: WOFF and WOFF 2.0 are not taken (ADR-0049).
    Unsupported = 2,
    /// A font past the limit.
    OverLimit = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(FontImportError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::font_import
