#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::authoring {

/// The domain of every Error authoring creates.
inline constexpr result::ErrorDomain kAuthoringDomain{base::parseBits128Hex("e5d9aa6df1e60e02b9181b9482ac90a5").value};

/// Codes within kAuthoringDomain: SPEC-0040's closed error classes, and the
/// delta grammar's own.
enum class AuthoringError : std::uint32_t {
    ValidationFailed = 1,
    TargetNotFound = 2,
    TargetStale = 3,
    CapabilityDenied = 4,
    LimitExceeded = 5,
    Conflict = 6,
    UnsupportedOperation = 7,
    Internal = 8,
    /// A delta out of its kind's shape, or a journal record out of its form.
    DeltaInvalid = 9,
    /// A delta applied to a slot that does not hold what it leaves from.
    DeltaMismatch = 10,
};

[[nodiscard]] constexpr result::ErrorCode code(AuthoringError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::authoring
