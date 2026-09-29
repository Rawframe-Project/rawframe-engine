#pragma once

// A game's input made the way ADR-0037 says a client makes it: controls into
// the game's action set, committed per tick, and the game's Kest sample
// function turning committed actions into its input component. Client only.

#include "rawframe/input/feed.h"
#include "rawframe/input/mapper.h"
#include "rawframe/kest/doors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/input_source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

/// The devices of the process's own player, lent by a client host: what
/// its window reported between two ticks.
inline constexpr composition::Capability<input::Feed> kFeed{"rawframe.input.feed"};

/// What the process's own player feels (D251): an effect the game says is
/// felt, through the devices its host lends. Provided with the input
/// sources, which hold the player's pairing.
class PlayerHaptics {
public:
    PlayerHaptics() = default;
    PlayerHaptics(const PlayerHaptics&) = delete;
    PlayerHaptics& operator=(const PlayerHaptics&) = delete;
    virtual ~PlayerHaptics() = default;

    /// Whether any effect of the game is felt.
    [[nodiscard]] virtual bool feelsEffects() const noexcept = 0;
    /// Effect kind `kind` delivered to the player: its haptic output is
    /// asked of the player's devices, and how many were asked is returned
    /// (none without a gamepad). Nothing for an effect not felt, or before
    /// the player has a source.
    virtual std::optional<std::size_t> feelEffect(std::uint32_t kind) = 0;
};

inline constexpr composition::Capability<PlayerHaptics> kPlayerHaptics{"rawframe.input_kest.player_haptics"};

struct SourceSettings {
    /// The game, whose description names its actions and sample program;
    /// outlives the call.
    const world_kest::GameFiles* game = nullptr;
    kest::CompileSettings compile;
    kest::MachineLimits limits{.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 1'000'000};
    /// The size of the game's input component, which the sample fills.
    std::size_t inputSize = 0;
    /// The player's devices, or null where the host lends none; outlives
    /// the sources.
    input::Feed* feed = nullptr;
};

/// The input sources of a game, and its player's haptics.
class InputSources : public world_replication::InputSourcePlan, public PlayerHaptics {};

/// Reads the game's controls and compiles its sample program once; each
/// bot source then has its own mapper, machine, and hand, and the player's
/// source its mapper and machine over the lent devices. Refuses
/// (`NotFound`, `NoControls`) a game without controls, and
/// (`UnknownHaptic`) an effect felt by a haptic output its actions lack.
[[nodiscard]] result::Result<std::unique_ptr<InputSources>> makeInputSources(const SourceSettings& settings);

} // namespace rawframe::input_kest
