#pragma once

#include <cstdint>

namespace rawframe::physics {

/// SPEC-0037's closed motion vocabulary, the same in both dimensions. Sleep
/// is a state a body is in, never a fourth motion.
enum class Motion : std::uint8_t {
    Static = 0,
    /// Moved by its velocity, immovable by contact.
    Kinematic = 1,
    Dynamic = 2,
};

} // namespace rawframe::physics
