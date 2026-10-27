#pragma once

// What a game's description says of the engine's own settings, read once a
// game participant has laid its program out: its physics and collision
// document, who is sent what (its interest line), and what a client
// predicts.

#include "game_scenes.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/physics/collision.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_replication/server.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The game's collision document, by identity.
[[nodiscard]] physics::CollisionDocument collisionDocumentOf(const GameDescription& game);

/// The 2D physics the game describes: its world and its collision document,
/// over those of its tilemaps that were read.
[[nodiscard]] physics2d::Physics2DSettings physics2dSettingsOf(const GameDescription& game,
                                                               const std::vector<GameTilemapResource>& tilemaps);

/// The same in three dimensions, over the game's meshes.
[[nodiscard]] physics3d::Physics3DSettings physics3dSettingsOf(const GameDescription& game,
                                                               const std::vector<physics3d::BodyMesh>& meshes);

/// Who is sent what: the interest line's coordinate fields, each a
/// floating-point number of the position component, `layouts` the game's
/// components' in declaration order. An entity leaves at an eighth beyond
/// the radius it entered at. None for a game without an interest line.
[[nodiscard]] result::Result<std::optional<world_replication::InterestSettings>>
interestOf(const GameDescription& game, std::span<const kest::TypeLayout> layouts);

/// The identities of `names`, each a component the game replicates: its
/// interpolated components, or those kept to their holder, `why`
/// the refusal of one it does not replicate.
[[nodiscard]] result::Result<std::vector<schema::ComponentTypeId>>
replicatedAmong(const GameDescription& game, std::span<const std::string> names, std::string_view why);

/// What a client predicts and steps it among.
struct PredictionPlan {
    std::vector<schema::ComponentTypeId> predicted;
    std::vector<schema::ComponentTypeId> nearby;
    /// The physics a predicting client steps its player's body in, and the
    /// level's static bodies, the values of each spawned one.
    std::optional<physics2d::Physics2DSettings> physics2d;
    std::optional<physics3d::Physics3DSettings> physics3d;
    std::vector<SpawnValues> level;
};

/// What a client predicts, checked against the rules: only the
/// player's replicated components, with input to predict from, by systems
/// that write no other replicated component and draw from no World stream.
/// Empty for a game that predicts nothing. `table` is what replicates.
[[nodiscard]] result::Result<PredictionPlan> predictionOf(const GameDescription& game,
                                                          const world_replication::ReplicationTable& table,
                                                          std::span<const kest::TypeLayout> layouts,
                                                          const GameFiles& files);

} // namespace rawframe::world_kest
