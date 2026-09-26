#pragma once

// The replication server's state, private to it: what it keeps of each
// connection and of the World between ticks. Input and admission are in
// server.cpp, publishing committed state to every connection in publish.cpp.

#include "peers.h"
#include "rawframe/network/close.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/server.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_replication {

/// State datagrams remembered per connection for acknowledgement. One not
/// acknowledged by the time it leaves this window was lost, as far as the
/// server is concerned, and its values go again.
inline constexpr std::size_t kSentWindow = 256;

/// A state acknowledgement covers its newest sequence and the 64 before it.
inline constexpr std::uint64_t kAckBits = 64;

/// What a connection's own player gains per tick: always more than anything
/// else can have waited.
inline constexpr std::uint64_t kOwnedPriority = std::uint64_t{1} << 40U;

/// Ticks of retired mappings the victim gate remembers: the most a physics
/// history keeps.
inline constexpr std::uint64_t kInterestKept = 1024;

/// Bytes a state datagram's framing takes around its payload, at most.
inline constexpr std::size_t kStateHeaderRoom = 3 * 8;

struct ReplicationServer::State {
    network::Sessions* sessions = nullptr;
    ServerReplicationSettings settings;
    std::map<std::uint64_t, Peer> peers;
    ServerReplicationStatistics statistics;
    /// The tick the last pump told, which input arriving then is before.
    std::uint64_t pumpTick = 0;
    std::uint32_t lastViewer = 0;
    std::vector<network::SessionEvent> events;

    // Resolved once the registry is frozen.
    std::vector<schema::ComponentRuntimeId> table;
    std::vector<schema::ComponentRuntimeId> playerComponents;
    std::optional<schema::ComponentRuntimeId> input;
    std::optional<schema::ComponentRuntimeId> perception;
    std::vector<world::ColumnQuery> queries;
    std::optional<world::ColumnQuery> positions;
    std::size_t positionSize = 0;
    std::vector<schema::ComponentRuntimeId> reads;
    std::vector<std::unique_ptr<world::System>> systems;
    std::vector<schema::ComponentRuntimeId> inputWrites;
    /// Each predicted component's replication table index, in the game's
    /// order, and the fingerprint of that scope (D204).
    std::vector<std::size_t> predicted;
    std::uint64_t scope = 0;
    std::vector<Divergence> divergences;
    std::vector<std::span<const std::byte>> scopeValues;

    // Scratch reused every tick. Every replicated value is encoded once per
    // tick, in entity then component order, and copied to each connection.
    struct PresentValue {
        world::EntityHandle entity;
        std::size_t component = 0;
        std::size_t offset = 0;
        std::size_t length = 0;
        /// The value in memory, while publish runs.
        const std::byte* source = nullptr;
    };
    std::vector<PresentValue> present;
    std::vector<std::byte> encoded;
    /// Values that name entities, encoded again for one connection, and
    /// where each is, by present index.
    std::vector<std::byte> named;
    std::vector<std::size_t> namedAt;
    /// Whether each table component's codec names entities, known once.
    std::vector<std::uint8_t> naming;
    /// Every positioned entity, in entity order, when interest is spatial.
    struct Located {
        world::EntityHandle entity;
        std::array<double, 3> at{};
    };
    std::vector<Located> located;
    /// Every entity with a replicated value, in entity order, and where it
    /// is: what each connection's mappings are walked along.
    struct PresentEntity {
        world::EntityHandle entity;
        bool located = false;
        std::array<double, 3> at{};
    };
    std::vector<PresentEntity> entities;
    std::vector<std::pair<std::uint32_t, std::size_t>> inDatagram;
    struct Candidate {
        std::uint64_t priority = 0;
        std::size_t present = 0;
        Mapping* mapping = nullptr;
    };
    std::vector<Candidate> candidates;
    std::vector<std::byte> records;
    std::vector<std::byte> datagram;
    std::vector<std::byte> frame;

    void sendMapping(Peer& peer, network::ControlFrame type, NetEntityId entity, bool owned);

    void onFrame(world::World& world, Peer& peer, const network::SessionEvent& event);

    void onStateAck(Peer& peer, const network::SessionEvent& event);

    void onInput(Peer& peer, const network::SessionEvent& event);

    void applyInputs(world::World& world);

    /// A consumed command's moment, checked when it arrived, kept within the
    /// skew of the lag its connection's claims have shown.
    [[nodiscard]] Perception
    perceived(Peer& peer, std::span<const std::byte> command, std::size_t wire, std::uint64_t arrived);

    void publish(world::World& world, world::TickIndex tick);

    void locate(world::World& world);

    /// Where a connection's player is, if it has a position.
    [[nodiscard]] const std::array<double, 3>* locationOf(world::EntityHandle entity) const noexcept;

    /// Whether `entity` is in the interest of a connection whose player is
    /// at `viewer`; `mapped` says whether it already is, and so whether it
    /// is held to the leaving radius or the entering one.
    [[nodiscard]] bool relevant(const Peer& peer,
                                const std::array<double, 3>* viewer,
                                const PresentEntity& entity,
                                bool mapped) const noexcept;

    [[nodiscard]] bool isPresent(world::EntityHandle entity) const noexcept;

    void sendPace(Peer& peer);

    /// SPEC-0041's checksum record (D204): looked at only within the rate
    /// limit, checked against the server's own checksum at its tick, and a
    /// mismatch recorded and counted, never acted on.
    void onChecksum(Peer& peer, const network::SessionEvent& event);

    /// The server's own checksum of the connection's predicted scope at the
    /// tick it publishes: its player's values as this tick encoded them.
    void keepChecksum(Peer& peer, world::TickIndex tick);

    void publishTo(Peer& peer, world::TickIndex tick);
};

} // namespace rawframe::world_replication
