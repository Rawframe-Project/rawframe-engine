#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::texture {

/// The domain of every Error the texture format and its importer create.
inline constexpr result::ErrorDomain kTextureDomain{base::parseBits128Hex("eb1495cfc2c7f778145d5847eba980c7").value};

/// Codes within kTextureDomain.
enum class TextureError : std::uint32_t {
    /// Bytes that are not a cooked texture as this format writes one, or a
    /// texture that breaks its rules: a level of the wrong size, levels
    /// that do not halve, bytes that do not fill a level.
    BadTexture = 1,
    /// Larger, or with more levels or bytes, than the limits allow.
    OverLimit = 2,
    /// A source image the importer cannot read: not PNG, JPEG, BMP, TGA,
    /// or Radiance, or broken.
    BadSource = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(TextureError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::texture
