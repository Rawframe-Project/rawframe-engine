#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::input {

/// The domain of every Error the input runtime creates. Documents are
/// refused in `rawframe.document`'s domain.
inline constexpr result::ErrorDomain kInputDomain{base::parseBits128Hex("07c22669743d6b1d3b2c17c0e5ee2bd7").value};

/// Codes within kInputDomain.
enum class InputError : std::uint32_t {
    /// A player slot, context, or device the mapper does not have.
    Unknown = 1,
    /// More devices or active routing nodes than the limits allow.
    TooMany = 2,
    /// A device already paired.
    AlreadyPaired = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(InputError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::input
