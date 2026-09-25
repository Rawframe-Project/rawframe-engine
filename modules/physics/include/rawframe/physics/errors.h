#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::physics {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kPhysicsDomain{base::parseBits128Hex("109aef5d1702a9c56c9c466f8195b293").value};

/// Codes within kPhysicsDomain.
enum class PhysicsError : std::uint32_t {
    /// A collision document that is not well formed (SPEC-0037 §15, 2 and 3).
    InvalidDocument = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(PhysicsError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::physics
