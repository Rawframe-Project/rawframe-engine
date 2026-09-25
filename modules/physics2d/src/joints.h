#pragma once

// Joints as Maul2D makes them (D114): private to the module, as Maul2D is.

#include "rawframe/physics2d/components.h"

#include <maul2d/maul2d.h>

namespace rawframe::physics2d {

/// One entity's joint: the joint made, what it was made of, and between
/// which bodies, to tell when either was made again.
struct MappedJoint {
    m2JointId joint{};
    Joint2D made;
    m2BodyId a{};
    m2BodyId b{};
    bool refused = false;
};

/// Field by field, for the padding a Joint2D has.
[[nodiscard]] bool same(const Joint2D& left, const Joint2D& right) noexcept;

/// The Maul2D joint a Joint2D is, between two bodies, by the axes it lets
/// move: a weld, a revolute, a prismatic along one frame axis, or a filter;
/// the null joint for any other shape of axes or values out of range.
/// Maul2D refuses the rest (a limit's order, a body pair it cannot join).
[[nodiscard]] m2JointId makeJoint(m2WorldId physics, const Joint2D& joint, m2BodyId a, m2BodyId b) noexcept;

} // namespace rawframe::physics2d
