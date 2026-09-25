#pragma once

// What the replication server keeps of each admitted connection: what it was
// sent of each entity, what it acknowledged, and its inputs waiting for
// their ticks. Private to the server.

#include "rawframe/network/admission.h"
#include "rawframe/network/provider.h"
#include "rawframe/world/entity.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/records.h"
#include "rawframe/world_runtime/players.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::world_replication {

/// Byte for byte, as one memcmp: std::ranges::equal compares std::byte one
/// at a time, and publish compares every value for every connection.
[[nodiscard]] inline bool sameBytes(std::span<const std::byte> left, std::span<const std::byte> right) noexcept {
    return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
}

/// What one connection was sent of one entity's component. The value the
/// client holds is `lastSent` once it acknowledges any datagram from
/// `changedAt` on: every record from then carries it.
struct Replica {
    std::vector<std::byte> lastSent;
    bool sent = false;
    std::uint64_t changedAt = 0;
    std::uint64_t sentAt = 0;
    std::optional<std::uint64_t> acknowledgedAt;
    /// Grows every tick the value needs sending and is not sent.
    std::uint64_t priority = 0;

    [[nodiscard]] bool held(std::span<const std::byte> value) const noexcept {
        return sent && acknowledgedAt.has_value() && *acknowledgedAt >= changedAt && sameBytes(lastSent, value);
    }
};

struct Mapping {
    NetEntityId id;
    bool acknowledged = false;
    /// The tick its first state record went out, for the victim gate.
    std::optional<std::uint64_t> firstSent;
    /// By replication table index.
    std::vector<Replica> replicas;
};

/// One state datagram sent and not yet known to have arrived.
struct SentState {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    bool acknowledged = false;
    /// By mapping ID, which an entity that left interest and came back
    /// does not share with its earlier mapping.
    std::vector<std::pair<std::uint32_t, std::size_t>> records;
};

/// An entity a connection was sent from one tick until another, before its
/// mapping was retired.
struct Sent {
    world::EntityHandle entity;
    std::uint64_t from = 0;
    std::uint64_t until = 0;
};

/// A command waiting for its tick, and the tick it arrived before.
struct Waiting {
    std::vector<std::byte> command;
    std::uint64_t arrived = 0;
};

/// One admitted connection.
struct Peer {
    network::ConnectionId connection;
    network::Accept accept;
    world::EntityHandle player;
    /// Who plays, if the connection asked for a session.
    std::optional<world_runtime::PlayerIdentity> identity;
    /// Its name in Perception and InterestHistory.
    std::uint32_t viewer = 0;
    std::vector<Sent> retired;
    /// Ticks its claimed moments lag their arrival, smoothed; none before
    /// the first claim.
    std::optional<double> lag;
    Perception seen;
    std::uint32_t nextNetEntity = 1;
    std::map<world::EntityHandle, Mapping> mapped;
    std::map<std::uint32_t, world::EntityHandle> byNetEntity;
    /// The next input tick to consume, and commands waiting for theirs.
    std::uint64_t nextInputTick = 0;
    std::uint64_t consumedInputTick = 0;
    std::map<std::uint64_t, Waiting> waitingInputs;
    std::vector<std::byte> lastCommand;
    std::uint32_t held = 0;
    std::uint64_t stateSequence = 0;
    std::deque<SentState> sent;
    /// How far ahead of consumption the newest command arrived, last seen.
    std::int64_t measuredLead = 0;
    bool heardInput = false;
};

/// An entity by the ID of the mapping a connection has acknowledged for it:
/// what it may be named by in a value sent there.
class PeerNames final : public EntityNames {
public:
    explicit PeerNames(const Peer& peer) noexcept : peer_(&peer) {
    }
    [[nodiscard]] std::uint32_t netOf(world::EntityHandle entity) const noexcept override {
        const auto kMapping = peer_->mapped.find(entity);
        return kMapping != peer_->mapped.end() && kMapping->second.acknowledged ? kMapping->second.id.value : 0;
    }
    [[nodiscard]] world::EntityHandle entityOf(std::uint32_t net) const noexcept override {
        const auto kFound = peer_->byNetEntity.find(net);
        return kFound != peer_->byNetEntity.end() ? kFound->second : world::EntityHandle{};
    }

private:
    const Peer* peer_;
};

} // namespace rawframe::world_replication
