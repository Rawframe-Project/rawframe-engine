#pragma once

// A Kest-scripted game as the engine loads it (D21). Kest types give each
// component its fields and widths; this text gives what Kest cannot say yet:
// stable identity, which functions are systems, and what the World starts
// with. It is parsed, never executed:
//
//   # A comment runs to the end of its line.
//   program movers.kest
//   component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 game.position Position
//   component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 game.velocity Velocity
//   system game.integrate simulation integrate write game.position read game.velocity
//   spawn 3 game.position x=0 y=0 game.velocity dx=1 dy=2
//
// `program` is relative to the description; a program that creates or names
// entities imports `rawframe.world`, and one that draws random numbers
// imports `rawframe.random`, both found through its `kest.project`. A
// `system` line takes an identity, a phase, the Kest function, then its
// columns in the function's order: `entities`, or `read`, `write`, `with`, or
// `without` before a component; `after` or `before` before another system of
// the same phase; and `random` before each stream it draws from. Every
// component may be inserted and removed by the program through
// `<Kest type>.insert` and `<Kest type>.remove`. A `spawn` line creates
// entities with the listed components; a field not given is zero. For
// networked play, `replicate` lists the components that replicate, `player`
// the components each connected player's entity starts with, and `input` the
// one component a player's input is written into. `predict` lists the
// player's components a client predicts, and a system marked `predicted` runs
// on predicting clients too, over the player alone (SPEC-0041); it may write
// no replicated component that is not predicted and draw from no World
// stream, whose state a client does not have. An `interest` line makes what
// each connection is sent spatial: an entity whose component's coordinate
// fields lie within the radius of the player's is sent, one without the
// component is sent to every connection, and the player always is:
//
//   interest game.position x y within 40
//
// An `entity` line names a component's fields that hold a
// `rawframe.world.Entity`, which a checkpoint writes as a reference rather
// than as numbers:
//
//   entity game.target who

#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/schedule.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The most lines a game description may have.
inline constexpr std::size_t kMaximumGameLines = 4096;
/// The most entities one spawn line may create.
inline constexpr std::uint32_t kMaximumSpawnCount = 1U << 20U;

struct GameComponent {
    schema::ComponentTypeId id;
    std::string name;
    std::string kestType;
};

struct GameColumn {
    world::Access access = world::Access::Read;
    std::string component;
    /// The archetype's entities rather than a component.
    bool entities = false;
};

struct GameSystem {
    std::string identity;
    world::Phase phase = world::Phase::Simulation;
    std::string entry;
    std::vector<GameColumn> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
    /// World random streams, drawn from by place through `rawframe.random`.
    std::vector<std::string> randomStreams;
    /// Runs on predicting clients too, over the player alone.
    bool predicted = false;
};

struct GameFieldValue {
    std::string field;
    std::string value;
};

struct GameSpawnComponent {
    std::string component;
    std::vector<GameFieldValue> fields;
};

struct GameSpawn {
    std::uint32_t count = 0;
    std::vector<GameSpawnComponent> components;
};

/// Spatial interest: the position component, its coordinate fields, and the
/// radius within which the player is sent another entity.
struct GameInterest {
    std::string component;
    std::vector<std::string> axes;
    double radius = 0;
};

/// A component field holding an entity, by its path in the Kest type.
struct GameEntityField {
    std::string component;
    std::string field;
};

struct GameDescription {
    std::string program;
    std::vector<GameComponent> components;
    std::vector<GameSystem> systems;
    std::vector<GameSpawn> spawns;
    /// Networked play: what replicates, what a player starts with, and
    /// which component a player's input is written into.
    std::vector<std::string> replicated;
    std::vector<std::string> player;
    std::string input;
    /// The player's components a client predicts from its own input.
    std::vector<std::string> predicted;
    std::vector<GameEntityField> entityFields;
    std::optional<GameInterest> interest;
};

/// Parses a description. Refuses (`invalid_argument`, with the line number as
/// context) an unknown keyword, a malformed ID or phase, a missing or second
/// `program`, a column or spawn naming a component the text does not declare,
/// and more than kMaximumGameLines lines.
[[nodiscard]] result::Result<GameDescription> parseGame(std::string_view text);

} // namespace rawframe::world_kest
