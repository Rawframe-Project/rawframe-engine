#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::scatter {

/// The domain of every Error the scatter format creates.
inline constexpr result::ErrorDomain kScatterDomain{base::parseBits128Hex("3dc93b11bac9a2fa30cfbb62a163a612").value};

/// Codes within kScatterDomain.
enum class ScatterError : std::uint32_t {
    /// Text that is not a scatter of this form, or one past its limits.
    BadSource = 1,
    /// A scatter on a terrain that does not name its layer.
    UnknownLayer = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(ScatterError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::scatter
