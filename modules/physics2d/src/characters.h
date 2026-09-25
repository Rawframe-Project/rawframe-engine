#pragma once

// The character controller's move (components.h's Character2D), on Maul2D's
// mover kit: private to the module, as Maul2D is.

#include "rawframe/physics2d/components.h"

#include <maul2d/maul2d.h>

namespace rawframe::physics2d {

/// Where a character's move took it, and what it ended on.
struct CharacterMove {
    m2Vec2 translation{0, 0};
    physics::Ground ground = physics::Ground::Airborne;
    m2Vec2 normal{0, 0};
};

/// Moves an upright capsule of `radius` and `halfHeight` posed at `from` as
/// far along `wish` (meters) as the solids `filter` sees allow, sliding along
/// what it meets, and says what it stands on after. `wasGrounded` and
/// `character.snap` step it down to ground it walked off. Reads the world
/// only.
[[nodiscard]] CharacterMove moveCharacter(m2WorldId world,
                                          float radius,
                                          float halfHeight,
                                          m2Transform from,
                                          m2Vec2 wish,
                                          const Character2D& character,
                                          bool wasGrounded,
                                          m2QueryFilter filter) noexcept;

} // namespace rawframe::physics2d
