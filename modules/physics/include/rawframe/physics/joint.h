#pragma once

#include <cstdint>

namespace rawframe::physics {

/// SPEC-0037's per-axis joint vocabulary (section 10), the same in both
/// dimensions: how a joint holds one axis of its frame.
enum class JointAxis : std::uint8_t {
    Locked = 0,
    Free = 1,
    /// Free between the axis's lower and upper limit.
    Limited = 2,
};

} // namespace rawframe::physics
