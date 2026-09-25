#pragma once

// Joints as Maul3D makes them (D114): private to the module, as Maul3D is.

#include "rawframe/physics3d/components.h"

#include <maul3d/maul3d.h>
#include <optional>

namespace rawframe::physics3d {

/// One entity's joint: the joint made, what it was made of, and between
/// which bodies, to tell when either was made again.
struct MappedJoint {
    m3JointId joint{};
    Joint3D made;
    m3BodyId a{};
    m3BodyId b{};
    bool refused = false;
};

/// Field by field, for the padding a Joint3D may have.
[[nodiscard]] bool same(const Joint3D& left, const Joint3D& right) noexcept;

/// The Maul3D generic joint a Joint3D is, between two bodies; none for
/// values out of range. Maul3D refuses the rest (a limit's order, the
/// angular contract, a motor on a locked axis) when the joint is made.
[[nodiscard]] std::optional<m3JointDef> jointDef(const Joint3D& joint, m3BodyId a, m3BodyId b) noexcept;

} // namespace rawframe::physics3d
