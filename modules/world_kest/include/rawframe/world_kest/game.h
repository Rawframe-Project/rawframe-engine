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
// `program` is relative to the description. A `system` line takes an
// identity, a phase, the Kest function, then `read`, `write`, `with`, or
// `without` before each component, and `after` or `before` before another
// system of the same phase. A `spawn` line creates entities with the listed
// components; a field not given is zero.

#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/schedule.h"

#include <cstddef>
#include <cstdint>
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
};

struct GameSystem {
    std::string identity;
    world::Phase phase = world::Phase::Simulation;
    std::string entry;
    std::vector<GameColumn> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
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

struct GameDescription {
    std::string program;
    std::vector<GameComponent> components;
    std::vector<GameSystem> systems;
    std::vector<GameSpawn> spawns;
};

/// Parses a description. Refuses (`invalid_argument`, with the line number as
/// context) an unknown keyword, a malformed ID or phase, a missing or second
/// `program`, a column or spawn naming a component the text does not declare,
/// and more than kMaximumGameLines lines.
[[nodiscard]] result::Result<GameDescription> parseGame(std::string_view text);

} // namespace rawframe::world_kest
