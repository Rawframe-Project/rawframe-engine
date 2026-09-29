#pragma once

// A game's guaranteed messages as doors (D266): for each message a game
// declares, `Messages.<name>(to: Entity, value: T)` sends one,
// `ReceivedCount.<name>() -> i32` says how many arrived for this tick, and
// `Received.<name>(index: i32) -> T` reads one. A server's doors stage what
// its systems send, kept only when the run that sent it succeeds; a client's
// present systems read what arrived since their last tick; a predictor's
// send nothing and have nothing arrived.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/program.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_replication/messages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_kest {

/// SPEC-0013's queued guaranteed events per connection, held per tick: a
/// server's systems send at most so many in one tick, together.
inline constexpr std::size_t kMaximumMessagesPerTick = 1024;

class MessageDoors final : public KestStaging {
public:
    enum class Role : std::uint8_t {
        /// A server: sends.
        Send,
        /// A client's presentation: reads.
        Read,
        /// Anywhere else: neither.
        Quiet,
    };

    /// Refuses a message whose type the program does not lay out, holds an
    /// entity, or is past network::kMaximumEventRecord.
    [[nodiscard]] static result::Result<std::unique_ptr<MessageDoors>>
    create(const GameDescription& game, const kest::Program& program, Role role);

    [[nodiscard]] result::Status addDoors(kest::DoorTable& doors);

    void begin() noexcept override;
    void end(bool kept) noexcept override;

    /// Send: appends what kept runs sent since last taken, in order.
    void take(std::vector<world_replication::PostedMessage>& into);
    /// Read: what the next tick's present systems read, replacing the last.
    void arrived(std::span<const world_replication::ReceivedMessage> messages);

    /// The largest message's bytes, at least one; nought for none.
    [[nodiscard]] std::size_t largest() const noexcept {
        return largest_;
    }

    struct Kind {
        MessageDoors* owner = nullptr;
        std::uint32_t kind = 0;
        std::size_t size = 0;
        std::string kestType;
        std::string send;
        std::string count;
        std::string read;
        std::array<kest::Parameter, 2> sendTakes{kest::Parameter{kest::Slot::Value, kEntityType},
                                                 kest::Parameter{kest::Slot::Value, ""}};
        std::array<kest::Parameter, 1> readGives{kest::Parameter{kest::Slot::Value, ""}};
        /// Arrived for this tick, in order.
        std::vector<std::span<const std::byte>> arrived;
    };

private:
    explicit MessageDoors(Role role) noexcept : role_(role) {
    }

    static void sendDoor(kest::DoorCall& call, void* context) noexcept;
    static void countDoor(kest::DoorCall& call, void* context) noexcept;
    static void readDoor(kest::DoorCall& call, void* context) noexcept;

    Role role_;
    std::size_t largest_ = 0;
    std::vector<std::unique_ptr<Kind>> kinds_;
    /// Sent by the run under way; kept by the runs that succeeded.
    std::vector<world_replication::PostedMessage> pending_;
    std::vector<world_replication::PostedMessage> kept_;
    std::vector<world_replication::ReceivedMessage> arrived_;
    std::vector<std::byte> scratch_;
};

} // namespace rawframe::world_kest
