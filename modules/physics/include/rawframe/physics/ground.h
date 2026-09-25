#pragma once

#include <cstdint>

namespace rawframe::physics {

/// What a character stands on after a step (SPEC-0037 §11's ground state),
/// the same in both dimensions.
enum class Ground : std::uint8_t {
    Airborne = 0,
    /// On a surface no steeper than the character's `groundNormal` allows.
    Grounded = 1,
    /// Pressed against a surface too steep to stand on.
    Sliding = 2,
};

} // namespace rawframe::physics
