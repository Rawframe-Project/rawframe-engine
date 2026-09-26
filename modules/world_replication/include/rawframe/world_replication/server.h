#pragma once

// The authoritative side of replication (SPEC-0010 authority and tick model,
// SPEC-0041 input consumption). Each admitted connection gets a player entity;
// its input window is decoded outside the tick and consumed exactly once per
// input tick in `apply_inputs`; committed state is read in `replication` and
// sent as self-sufficient state datagrams for entities whose mapping the
// client has acknowledged, for the entities in that connection's interest.

#include "rawframe/network/session.h"
#include "rawframe/result/result.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/records.h"
#include "rawframe/world_runtime/players.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::world_replication {

/// What replicates, in wire order: the index of a codec here is its
/// component's number in every state record. Both sides hold the same table.
struct ReplicationTable {
    std::vector<ComponentCodec> components;
};

/// Spatial interest (SPEC-0010; SPEC-0041's owned-entity invariant). A
/// connection's own player is always in its interest, before any other rule
/// or bound. Another entity with the position component enters when it lies
/// within `radius` of the player and leaves beyond `leaveRadius`, so one at
/// the edge does not come and go every tick; one without the component is
/// in every connection's interest. A player without a position sees only
/// itself and what has none. An entity that leaves is retired from the
/// connection, as though it were gone, and one that comes back is declared
/// afresh under a new ID.
struct InterestSettings {
    schema::ComponentTypeId position;
    /// One to three coordinates, each an F32 or F64 field of the component.
    std::vector<WireField> axes;
    double radius = 0;
    double leaveRadius = 0;
};

struct ServerReplicationSettings {
    ReplicationTable table;
    /// Components a player entity starts with, zeroed.
    std::vector<schema::ComponentTypeId> playerComponents;
    /// The component a connection's input commands are written into, one of
    /// the player's; none for a game without input.
    std::optional<ComponentCodec> input;
    /// Each command carries the moment its client saw, which is written
    /// into the player's Perception (perception.h), a player component.
    bool perception = false;
    /// SPEC-0041's perception_skew_max: ticks a claimed moment may lie from
    /// the lag the server measures for its connection (the smoothed lag of
    /// earlier claims behind their arrival) before it is clamped.
    double perceptionSkew = 6;
    /// Every entity is in every connection's interest without one.
    std::optional<InterestSettings> interest;
    /// Entities one connection may have mapped at once.
    std::size_t maximumMapped = 4096;
    /// Input ticks a command may run ahead of the newest consumed one.
    std::uint64_t inputFutureWindow = 32;
    /// Ticks the last command is held for when the next has not arrived,
    /// before the input goes neutral (zero).
    std::uint32_t inputHoldLast = 4;
    /// How many ticks ahead of consumption input should arrive, and how
    /// often each connection is told how far ahead its input does arrive.
    std::uint64_t targetInputLead = 2;
    std::uint64_t paceInterval = 10;
    /// Ticks from one publish of state to a connection to its next:
    /// SPEC-0013's current-state production cadence (D222). Connections are
    /// spread across a period's ticks, so each tick publishes to about a
    /// period's share of them. One publishes to every connection every
    /// tick.
    std::uint32_t statePeriod = 1;
    /// State bytes one connection is sent per publish, datagram framing
    /// included and the transport's own not. When more has changed than
    /// fits, what has waited longest goes first and the rest waits; the
    /// connection's own player always goes first. The default is SPEC-0013's
    /// 64 KiB/s at 60 publishes a second.
    std::size_t stateBytesPerPublish = 1092;
    /// Ticks after which a value sent and not acknowledged is sent again,
    /// though it has not changed.
    std::uint64_t resendAfter = 6;
    /// Told of each player with an identity (a session asked for) as their
    /// entity is made and before it goes; none tells no one.
    world_runtime::PlayerPresence* presence = nullptr;
    /// The player's components clients predict, in the game's order, all
    /// replicated: the scope a client's checksum records are of. None takes
    /// no checksum records.
    std::vector<schema::ComponentTypeId> predicted;
    /// SPEC-0041's checksum_rate_max: records looked at per connection per
    /// second; the rest are dropped unread.
    std::uint32_t checksumsPerSecond = 8;
};

/// SPEC-0041's `prediction_divergence`: a connection's checksum of its
/// predicted scope at a tick the server published was not the server's.
struct Divergence {
    network::ConnectionId connection;
    std::uint64_t tick = 0;
    std::uint64_t scope = 0;
    std::uint64_t expected = 0;
    std::uint64_t received = 0;
};

/// What the server counted.
struct ServerReplicationStatistics {
    std::uint64_t stateDatagrams = 0;
    /// Their payload bytes, all connections together.
    std::uint64_t stateBytes = 0;
    /// State records sent, and left out because the client held the value.
    std::uint64_t recordsSent = 0;
    std::uint64_t recordsHeld = 0;
    /// Left for a later tick by the per-connection byte budget.
    std::uint64_t recordsDeferred = 0;
    std::uint64_t acknowledgementsRefused = 0;
    /// Mappings retired because the entity left a connection's interest.
    std::uint64_t interestLeft = 0;
    std::uint64_t inputsConsumed = 0;
    std::uint64_t inputsHeld = 0;
    std::uint64_t inputsNeutral = 0;
    std::uint64_t inputsRefused = 0;
    /// Claimed moments moved to within the skew.
    std::uint64_t perceptionsClamped = 0;
    /// Checksum records that matched, that could not be checked (another
    /// scope, a tick no longer kept), that did not match, and that were
    /// dropped by the rate limit.
    std::uint64_t checksumsVerified = 0;
    std::uint64_t checksumsUnverifiable = 0;
    std::uint64_t checksumsDiverged = 0;
    std::uint64_t checksumsLimited = 0;
    /// Malformed payloads struck against their connections (D223).
    std::uint64_t strikes = 0;
};

class ReplicationServer final : public world_runtime::SystemContributor, public InterestHistory {
public:
    /// `sessions` is a server's and must outlive this.
    [[nodiscard]] static result::Result<std::unique_ptr<ReplicationServer>> create(network::Sessions& sessions,
                                                                                   ServerReplicationSettings settings);
    ~ReplicationServer() override;

    /// Contributes `rawframe.replication.apply_inputs` (apply_inputs) and
    /// `rawframe.replication.publish` (replication).
    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Between ticks: drains the sessions, admits players (creating their
    /// entities directly, since no system runs), takes mapping
    /// acknowledgements, and stores input windows. `tick` is the next tick
    /// the World will run, told to new connections as their origin.
    void pump(world::World& world, world::TickIndex tick);

    /// The World was replaced (a checkpoint restore): every connection's
    /// entities and mappings named the old one, so every connection is
    /// closed and its players reconnect into the new World (SPEC-0010: a
    /// remap is a new epoch, never a partial one).
    void forgetWorld() noexcept;

    [[nodiscard]] ServerReplicationStatistics statistics() const noexcept;
    /// Bytes it holds for its connections and its publishing, at the
    /// capacity each table has grown to, counting a node-based container's
    /// entries by their size: SPEC-0013's replication attribution (D216).
    [[nodiscard]] std::size_t heldBytes() const noexcept;
    /// Admitted connections, each with its player.
    [[nodiscard]] std::size_t connections() const noexcept;
    /// Tells every admitted connection the server is stopping (SPEC-0012's
    /// stopping notice): one `graceful_close` each. Play goes on.
    void noticeStopping() noexcept;
    /// Whether a connection is playing as `identity` now.
    [[nodiscard]] bool playing(world_runtime::PlayerIdentity identity) const noexcept;
    /// The server is stopping: each player with an identity is told of as
    /// leaving, their entity still there.
    void leaveAll(world::World& world) noexcept;
    /// The player entity of an admitted connection, or the null handle.
    [[nodiscard]] world::EntityHandle player(network::ConnectionId connection) const noexcept;
    /// Divergences found since last asked, the oldest first, at most 64; a
    /// detection, never a response (SPEC-0041).
    [[nodiscard]] std::vector<Divergence> takeDivergences();
    /// How many of a connection's checksum records did not match.
    [[nodiscard]] std::uint64_t divergences(network::ConnectionId connection) const noexcept;

    [[nodiscard]] std::optional<std::uint64_t>
    sentSince(std::uint32_t viewer, world::EntityHandle entity, std::uint64_t tick) const noexcept override;

    struct State;
    explicit ReplicationServer(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_replication
