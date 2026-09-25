#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_animation {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldAnimationDomain{
    base::parseBits128Hex("6b239aa89c0f6c950416f89f11bea12d").value};

/// Codes within kWorldAnimationDomain.
enum class WorldAnimationError : std::uint32_t {
    /// Settings out of their rules: an animator twice, a parameter bound
    /// twice or past its component, or a World without the engine's
    /// animation component or an animator's parameter component.
    InvalidSettings = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldAnimationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_animation
