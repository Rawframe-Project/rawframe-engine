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

/// State datagrams remembered per connection for acknowledgement, at most:
/// those an acknowledgement can no longer reach are forgotten as soon as a
/// newer one arrives (D217), so this bounds only a connection that stops
/// acknowledging. One not acknowledged by the time it leaves was lost, as
/// far as the server is concerned, and its values go again.
inline constexpr std::size_t kSentWindow = 256;

/// A state acknowledgement covers its newest sequence and the 64 before it.
inline constexpr std::uint64_t kAckBits = 64;

/// What a connection's own player gains per tick: always more than anything
/// else can have waited.
inline constexpr std::uint64_t kOwnedPriority = std::uint64_t{1} << 40U;

/// Ticks of retired mappings the victim gate remembers: the most a physics
/// history keeps.
inline constexpr std::uint64_t kInterestKept = 1024;
/// No place in a table by slot.
inline constexpr std::size_t kNowhere = SIZE_MAX;

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
    /// The values as the queries gave them, before they are in order.
    std::vector<PresentValue> gathered;
    /// Where each slot's values start in `present`, and one past the last
    /// slot; and the next place for each while they are put in order.
    std::vector<std::size_t> valuesAt;
    std::vector<std::size_t> filling;
    std::vector<std::byte> encoded;
    /// Values that name entities, encoded again for one connection.
    std::vector<std::byte> named;
    /// Whether each table component's codec names entities, known once.
    std::vector<std::uint8_t> naming;
    /// Every positioned entity, when interest is spatial, and where each is
    /// in it by slot.
    struct Located {
        world::EntityHandle entity;
        std::array<double, 3> at{};
    };
    std::vector<Located> located;
    std::vector<std::size_t> locatedAt;
    /// Every entity with a replicated value, in entity order, and where it
    /// is: what each connection's mappings are walked along.
    struct PresentEntity {
        world::EntityHandle entity;
        bool located = false;
        std::array<double, 3> at{};
    };
    std::vector<PresentEntity> entities;
    /// Where each slot's entity is in `entities`, or nowhere.
    std::vector<std::size_t> entityAt;
    /// Positioned entities by interest cell, when interest is spatial
    /// (D209): cells as wide as the entering radius, so everything a viewer
    /// may enter is in the cells around its own. Cells are hashed into
    /// buckets; `bucketed` holds indices into `entities`, bucket by bucket
    /// and in entity order within one, and `bucketAt` where each bucket
    /// starts, with one past the last. A bucket may hold several cells.
    std::vector<std::size_t> bucketed;
    std::vector<std::size_t> bucketAt;
    std::vector<std::size_t> bucketOf;
    std::size_t bucketMask = 0;
    /// Indices into `entities` of those without a position: in every
    /// connection's interest.
    std::vector<std::size_t> unplaced;
    /// One connection's entities that may enter, by bucket, and those that
    /// do.
    std::vector<std::size_t> reachable;
    std::vector<std::size_t> entering;
    /// Which entities the connection being published to holds: those
    /// marked `markNow`, which counts connections through a tick.
    std::vector<std::uint64_t> heldMark;
    std::uint64_t markNow = 0;
    std::vector<std::pair<std::uint32_t, std::size_t>> inDatagram;
    struct Candidate {
        std::uint64_t priority = 0;
        std::size_t present = 0;
        /// Where a value that names entities was encoded for this
        /// connection in `named`.
        std::size_t named = 0;
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

    /// Files `entities` into `cells` and `unplaced`.
    void cellEntities();

    /// Fills `reachable` with what a connection whose player is at `viewer`
    /// may enter, in no order: what its player's cell and those around it
    /// hold, and every entity without a position.
    void reach(const std::array<double, 3>& viewer);

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

    /// Where `entity` is in `entities` this tick, or kNowhere.
    [[nodiscard]] std::size_t presentIndex(world::EntityHandle entity) const noexcept;

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
