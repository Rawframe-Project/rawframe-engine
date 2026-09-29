#pragma once

// A game's guaranteed messages (D266): a server system sends one player a
// value of a type the game declares; it arrives in the order sent, on the
// game's event lane (SPEC-0010 guaranteed events), once the tick that sent
// it has committed. Messages to a player who is not connected are counted
// and go nowhere: the promise is to a connection, not to an identity.

#include "rawframe/network/session.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::world_replication {

/// The game's event lane. Lane nought is kept for the engine's own.
inline constexpr std::uint64_t kGameMessageLane = 1;

/// The lane both sides declare for a game whose largest message is `record`
/// bytes, 1 to network::kMaximumEventRecord.
[[nodiscard]] inline network::EventLaneDeclaration gameMessageLane(std::size_t record) noexcept {
    return {.id = kGameMessageLane, .fromServer = true, .maximumRecord = record};
}

/// What a server's systems sent in a tick: to which player, which of the
/// game's messages, by its place among them, and its value's bytes.
struct PostedMessage {
    world::EntityHandle player;
    std::uint32_t kind = 0;
    std::vector<std::byte> value;
};

struct ReceivedMessage {
    std::uint32_t kind = 0;
    std::vector<std::byte> value;
};

/// Where a client's received messages go, in the order they arrived.
class MessageSink {
public:
    MessageSink() = default;
    MessageSink(const MessageSink&) = delete;
    MessageSink& operator=(const MessageSink&) = delete;
    virtual ~MessageSink() = default;

    virtual void deliver(ReceivedMessage message) noexcept = 0;
};

} // namespace rawframe::world_replication
