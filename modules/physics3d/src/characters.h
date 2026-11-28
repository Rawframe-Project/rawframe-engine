#pragma once

// The character controller's move (components.h's Character3D), on Maul3D's
// mover kit: private to the module, as Maul3D is.

#include "rawframe/physics/ground.h"
#include "rawframe/physics3d/components.h"

#include <array>
#include <cstdint>
#include <maul3d/maul3d.h>

namespace rawframe::physics3d {

/// A shove a move gives a dynamic body that stopped it: an impulse at a
/// point on the body's surface.
struct CharacterPush {
    m3BodyId body{};
    m3Vec3 impulse{0, 0, 0};
    m3Pos3 point{0, 0, 0};
};

/// Where a character's move took it, what it ended on, and the bodies it
/// pushes.
struct CharacterMove {
    m3Vec3 translation{0, 0, 0};
    physics::Ground ground = physics::Ground::Airborne;
    m3Vec3 normal{0, 0, 0};
    /// What it stands on while Grounded.
    m3ShapeId groundShape{};
    /// Whether it climbed a step, and how far up: the part of the
    /// translation made at once, before the step, rather than as velocity.
    bool climbed = false;
    float lift = 0;
    std::array<CharacterPush, M3_MOVER_PLANES> pushes{};
    std::int32_t pushCount = 0;
};

/// An upright capsule of `radius` and `halfHeight` centered somewhere, and
/// the solids `filter` sees: what every query of a character's is about.
struct CharacterShape {
    float radius = 0;
    float halfHeight = 0;
    m3QueryFilter filter{};
};

/// Moves the capsule centered at `from` as far along `wish` (meters) as the
/// solids allow, sliding along what it meets, and says what it stands on
/// after. `wasGrounded` and the character's `snap` step it down to ground it
/// walked off, and its `stepHeight` up onto a step it walked into;
/// `seconds` is the tick's length, which a push's impulse is over. Reads the
/// world only.
[[nodiscard]] CharacterMove moveCharacter(const m3World* world,
                                          const CharacterShape& shape,
                                          m3Pos3 from,
                                          m3Vec3 wish,
                                          const Character3D& character,
                                          bool wasGrounded,
                                          float seconds) noexcept;

/// Whether the capsule has room at `at`: nothing it would overlap there,
/// ground a skin under it aside.
[[nodiscard]] bool characterFits(const m3World* world, const CharacterShape& shape, m3Pos3 at) noexcept;

/// How much of `translation` the capsule at `from` sweeps clear of what it
/// meets: all of it when it starts inside something.
[[nodiscard]] m3Vec3
sweepCharacter(const m3World* world, const CharacterShape& shape, m3Pos3 from, m3Vec3 translation) noexcept;

} // namespace rawframe::physics3d
