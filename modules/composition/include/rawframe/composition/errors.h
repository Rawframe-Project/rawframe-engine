#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::composition {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kCompositionDomain{
    base::parseBits128Hex("780cffbc0e0276b425dc21cec0470e0a").value};

/// Codes within kCompositionDomain.
enum class CompositionError : std::uint32_t {
    PlanRefused = 1,
    CapabilityNotResolved = 2,
    CapabilityTypeMismatch = 3,
    CapabilityNotProvided = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(CompositionError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::composition
