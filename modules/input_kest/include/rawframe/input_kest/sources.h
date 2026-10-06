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
#include "rawframe/view/players.h"
#include "rawframe/view/pointing.h"
#include "rawframe/world_kest/commands.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/input_source.h"
#include "rawframe/world_replication/messages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

/// What the `View.*` doors read: the local players' views, and whose.
struct ViewDoorContext {
    const view::PlayerViews* views = nullptr;
    std::size_t player = 0;
};

/// Adds the doors `rawframe.view` asks for (ADR-0052, D367):
/// `View.scenePlace` and `View.canvasPlace`, where the player's view is in
/// its window; `View.pointToRay` and `View.worldToPoint` through its scene
/// camera; `View.pointToWorld2D` and `View.world2DToPoint` through its
/// canvas camera. Each answers a failure number, never a NaN: no view
/// (none lent, or none of the kind told), or the view's own closed set.
/// They read and change nothing else, so each is safe for untrusted code.
/// `context` outlives every machine started with the table.
[[nodiscard]] result::Status addViewDoors(kest::DoorTable& doors, const ViewDoorContext* context);

/// What the `Commands.*` doors keep: for each of the game's commands the
/// sample program lays out, its door, and what one sample call sent, at
/// most world_kest::kMaximumCommandsPerTick (D425).
struct CommandDoorContext {
    struct Kind {
        CommandDoorContext* owner = nullptr;
        world_kest::CommandKind command;
        std::string send;
        std::array<kest::Parameter, 1> takes{kest::Parameter{kest::Slot::Value, ""}};
    };
    std::vector<std::unique_ptr<Kind>> kinds;
    std::vector<world_replication::PostedCommand> sent;
};

/// Adds `Commands.<name>(value)` for each kind `context` holds: the value
/// sent to the server after the tick's input. Trusted code only, as the
/// sample is. `context` outlives every machine started with the table.
[[nodiscard]] result::Status addCommandDoors(kest::DoorTable& doors, CommandDoorContext& context);

/// What the `UI.*` doors read: the press code the player's press since the
/// last tick landed on, the first of them, nought for none (D421).
struct UiDoorContext {
    std::int64_t pressed = 0;
};

/// Adds `UI.pressed`. It reads and changes nothing else, so it is safe for
/// untrusted code. `context` outlives every machine started with the table.
[[nodiscard]] result::Status addUiDoors(kest::DoorTable& doors, const UiDoorContext* context);

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
    /// How they are paired to the local players as they connect (D363):
    /// merged for one, keyboard first for more, unless named.
    std::optional<input::PairingPolicy> pairing;
    /// The local players' views (D367), or null where the host lends
    /// none; outlives the sources.
    const view::PlayerViews* views = nullptr;
    /// What the UI takes of the pointer (D421), or null where the host
    /// lends nothing; outlives the sources.
    const view::UiPointing* pointing = nullptr;
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
