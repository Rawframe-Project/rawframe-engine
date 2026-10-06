#pragma once

// A game's commands as a program lays them out (D425): the kinds a client's
// sample function may send and the server's systems read, each by its
// place among the game's command lines and the exact size of its value,
// which is all the server knows to check a hostile one by.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::world_kest {

/// The largest command's value: a command is a player's request, not a
/// transfer.
inline constexpr std::size_t kMaximumCommandRecord = 1024;
/// The most commands one sample call sends: past it the door fails.
inline constexpr std::size_t kMaximumCommandsPerTick = 8;

struct CommandKind {
    /// Its place among the game's command lines.
    std::uint32_t kind = 0;
    std::string name;
    std::string kestType;
    std::size_t size = 0;
};

/// The game's commands `program` lays out, in their lines' order. A type
/// that holds an entity, or is past kMaximumCommandRecord, is refused
/// (`bad_game_line`); one the program does not lay out is refused when
/// `everyOne` (the game's own program, which reads them all), else left out
/// (a sample that never names it sends none).
[[nodiscard]] result::Result<std::vector<CommandKind>>
commandKindsOf(const GameDescription& game, const kest::Program& program, bool everyOne);

} // namespace rawframe::world_kest
