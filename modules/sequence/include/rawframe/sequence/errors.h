#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::sequence {

/// The domain of every Error the sequence format creates.
inline constexpr result::ErrorDomain kSequenceDomain{base::parseBits128Hex("91ad8f7d6411db09055fce326a632379").value};

/// Codes within kSequenceDomain.
enum class SequenceError : std::uint32_t {
    /// Text that is not a sequence of this form, or one past its limits.
    BadSource = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(SequenceError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::sequence
