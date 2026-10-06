#pragma once

// A player's commands as doors (D425): for each command a game declares,
// `CommandCount.<name>() -> i32` says how many the tick's systems may read,
// `Command.<name>(index: i32) -> T` reads one, and `CommandFrom.<name>(index:
// i32) -> Entity` names the player who sent it. A server's doors read what
// its players sent, each command at the first tick at or after the one it
// was delivered for; every other machine's have none. Trusted code only, as
// every door that names a player is.

#include "rawframe/kest/doors.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/commands.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_replication/messages.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_kest {

class CommandDoors final : public KestStaging {
public:
    enum class Role : std::uint8_t {
        /// A server: reads what its players sent.
        Read,
        /// Anywhere else: has none.
        Quiet,
    };

    /// Refuses what commandKindsOf refuses of the game's own program.
    [[nodiscard]] static result::Result<std::unique_ptr<CommandDoors>>
    create(const GameDescription& game, const kest::Program& program, Role role);

    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    void begin(world::TickIndex tick) noexcept override;
    void end(bool kept) noexcept override;

    /// Read: commands checked by the replication server, each for the tick
    /// it names. One still unread when commands for two ticks after its own
    /// arrive is dropped.
    void deliver(std::span<const world_replication::ReceivedCommand> commands);

    /// Each command's exact size, by its place among the game's lines.
    [[nodiscard]] std::span<const std::size_t> sizes() const noexcept {
        return sizes_;
    }

    struct Kind {
        CommandDoors* owner = nullptr;
        CommandKind command;
        std::string count;
        std::string read;
        std::string from;
        std::array<kest::Parameter, 1> readGives{kest::Parameter{kest::Slot::Value, ""}};
        /// Readable this tick, in the order they arrived.
        std::vector<const world_replication::ReceivedCommand*> current;
    };

private:
    explicit CommandDoors(Role role) noexcept : role_(role) {
    }

    static void countDoor(kest::DoorCall& call, void* context) noexcept;
    static void readDoor(kest::DoorCall& call, void* context) noexcept;
    static void fromDoor(kest::DoorCall& call, void* context) noexcept;
    /// The command at the call's index, or none, the call failed.
    static const world_replication::ReceivedCommand* at(kest::DoorCall& call, const Kind& kind) noexcept;

    Role role_;
    std::vector<std::unique_ptr<Kind>> kinds_;
    std::vector<std::size_t> sizes_;
    /// Delivered for a tick not yet run.
    std::vector<world_replication::ReceivedCommand> delivered_;
    /// The tick the current ones are for, and they.
    std::optional<world::TickIndex> tick_;
    std::vector<world_replication::ReceivedCommand> current_;
};

} // namespace rawframe::world_kest
