#pragma once

// A game's input made the way ADR-0037 says a client makes it: controls into
// the game's action set, committed per tick, and the game's Kest sample
// function turning committed actions into its input component. Client only.

#include "rawframe/input/mapper.h"
#include "rawframe/kest/doors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/input_source.h"

#include <cstddef>
#include <memory>
#include <string>

namespace rawframe::input_kest {

/// What the `Input.*` doors read: one player's committed actions.
struct InputDoorContext {
    const input::Mapper* mapper = nullptr;
    input::PlayerSlot player;
};

/// Adds `Input.on`, `Input.pressed`, `Input.released`, `Input.x`, and
/// `Input.y`. Each takes an action's identity and refuses one the set does
/// not have. They read and change nothing else, so each is safe for
/// untrusted code. `context` outlives every machine started with the table.
[[nodiscard]] result::Status addInputDoors(kest::DoorTable& doors, const InputDoorContext* context);

struct SourceSettings {
    /// The game, whose description names its actions and sample program;
    /// outlives the call.
    const world_kest::GameFiles* game = nullptr;
    kest::CompileSettings compile;
    kest::MachineLimits limits{.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 1'000'000};
    /// The size of the game's input component, which the sample fills.
    std::size_t inputSize = 0;
};

/// Reads the game's controls and compiles its sample program once; each
/// bot source then has its own mapper, machine, and hand. Refuses
/// (`NotFound`, `NoControls`) a game without controls.
[[nodiscard]] result::Result<std::unique_ptr<world_replication::InputSourcePlan>>
makeInputSources(const SourceSettings& settings);

} // namespace rawframe::input_kest
