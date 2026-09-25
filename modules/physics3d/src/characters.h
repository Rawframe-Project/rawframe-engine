#pragma once

// The character controller's move (components.h's Character3D), on Maul3D's
// mover kit: private to the module, as Maul3D is.

#include "rawframe/physics/ground.h"
#include "rawframe/physics3d/components.h"

#include <maul3d/maul3d.h>

namespace rawframe::physics3d {

/// Where a character's move took it, and what it ended on.
struct CharacterMove {
    m3Vec3 translation{0, 0, 0};
    physics::Ground ground = physics::Ground::Airborne;
    m3Vec3 normal{0, 0, 0};
};

/// Moves an upright capsule of `radius` and `halfHeight` centered at `from`
/// as far along `wish` (meters) as the solids `filter` sees allow, sliding
/// along what it meets, and says what it stands on after. `wasGrounded` and
/// `character.snap` step it down to ground it walked off. Reads the world
/// only.
[[nodiscard]] CharacterMove moveCharacter(m3WorldId world,
                                          float radius,
                                          float halfHeight,
                                          m3Pos3 from,
                                          m3Vec3 wish,
                                          const Character3D& character,
                                          bool wasGrounded,
                                          m3QueryFilter filter) noexcept;

} // namespace rawframe::physics3d
