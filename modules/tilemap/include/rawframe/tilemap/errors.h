#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::tilemap {

/// The domain of every Error the tilemap format creates.
inline constexpr result::ErrorDomain kTilemapDomain{base::parseBits128Hex("ef24b84f8980944b4889d586e34c24eb").value};

/// Codes within kTilemapDomain.
enum class TilemapError : std::uint32_t {
    /// Text that is not a tilemap of this form, or one past its limits.
    BadSource = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(TilemapError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::tilemap
