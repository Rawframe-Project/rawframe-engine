#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::physics2d {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kPhysics2DDomain{base::parseBits128Hex("b6ef509b7c828fe54ba2e3aa3d0f7233").value};

/// Codes within kPhysics2DDomain.
enum class Physics2DError : std::uint32_t {
    /// This processor cannot run the physics build's kernels.
    Unsupported = 1,
    /// Settings out of range, or a World whose physics components are not
    /// the engine's.
    InvalidSettings = 2,
    /// No more physics worlds in this process, or a full world.
    Capacity = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(Physics2DError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::physics2d
