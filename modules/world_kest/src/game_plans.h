#pragma once

// What a game's description says of the engine's own settings, read once a
// game participant has laid its program out: its physics and collision
// document, and who is sent what (its interest line).

#include "rawframe/kest/program.h"
#include "rawframe/physics/collision.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/plan.h"

#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_kest {

/// The game's collision document, by identity.
[[nodiscard]] physics::CollisionDocument collisionDocumentOf(const GameDescription& game);

/// The 2D physics the game describes: its world and its collision document.
[[nodiscard]] physics2d::Physics2DSettings physics2dSettingsOf(const GameDescription& game);

/// The same in three dimensions, over the game's meshes.
[[nodiscard]] physics3d::Physics3DSettings physics3dSettingsOf(const GameDescription& game,
                                                               const std::vector<physics3d::BodyMesh>& meshes);

/// Who is sent what: the interest line's coordinate fields, each a
/// floating-point number of the position component, `layouts` the game's
/// components' in declaration order. An entity leaves at an eighth beyond
/// the radius it entered at. None for a game without an interest line.
[[nodiscard]] result::Result<std::optional<world_replication::InterestSettings>>
interestOf(const GameDescription& game, std::span<const kest::TypeLayout> layouts);

} // namespace rawframe::world_kest
