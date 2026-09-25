#pragma once

// The receiving side of replication: a client World that mirrors what the
// server replicates, and the client's input sent as SPEC-0041 windows. Mapped
// entities are made and removed between local ticks, as the server declares
// and retires them; a state datagram is decoded whole and staged before any
// of it is applied, and a value older than the one already applied is not.
// Remote entities' interpolated components are shown a little in the past,
// between two received states (interpolation.h).

#include "rawframe/network/session.h"
#include "rawframe/result/result.h"
#include "rawframe/world/world.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/interpolation.h"
#include "rawframe/world_replication/prediction.h"
#include "rawframe/world_replication/records.h"
#include "rawframe/world_replication/server.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace rawframe::world_replication {

struct ClientReplicationSettings {
    /// The same table the server replicates with.
    ReplicationTable table;
    /// The input component's codec, when the game takes input.
    std::optional<ComponentCodec> input;
    /// Each command carries the moment shown when it was given: the one
    /// interpolation shows, or the newest state without it.
    bool perception = false;
    /// Entities mirrored at once.
    std::size_t maximumMapped = 4096;
    /// Predicting the player's own components from its own input; none
    /// shows the server's state only.
    std::optional<PredictionSettings> prediction;
    /// Showing remote entities between states; none shows each state as
    /// it arrives.
    std::optional<InterpolationSettings> interpolation;
};

struct ClientReplicationStatistics {
    std::uint64_t stateDatagrams = 0;
    std::uint64_t recordsApplied = 0;
    /// Older than what was already applied for that entity and component.
    std::uint64_t recordsStale = 0;
    /// Naming an entity with no live mapping: never buffered.
    std::uint64_t recordsUnmapped = 0;
    /// Refused whole: wrong epoch or malformed.
    std::uint64_t datagramsRefused = 0;
    std::uint64_t inputWindowsSent = 0;
    std::uint64_t acknowledgementsSent = 0;
};

class ReplicationClient {
public:
    /// `sessions` is a client's; `world` is the mirror, whose registry holds
    /// every table component. Both must outlive this.
    [[nodiscard]] static result::Result<std::unique_ptr<ReplicationClient>>
    create(network::Sessions& sessions, world::World& world, ClientReplicationSettings settings);
    ~ReplicationClient();

    [[nodiscard]] result::Status connect(const network::Endpoint& endpoint, const network::Hello& hello);

    /// Between local ticks: drains the session and applies what arrived.
    void pump();

    [[nodiscard]] bool admitted() const noexcept;
    /// What the server granted, once admitted.
    [[nodiscard]] const std::optional<network::Accept>& accept() const noexcept;
    /// Whether the session is over, admitted or not.
    [[nodiscard]] bool ended() const noexcept;
    /// The mirror of the entity this client plays, once declared.
    [[nodiscard]] world::EntityHandle owned() const noexcept;
    /// The newest server tick applied.
    [[nodiscard]] std::uint64_t serverTick() const noexcept;

    /// The input for the next input tick, as a value of the input component
    /// (its in-memory bytes), sent with every command not yet consumed.
    [[nodiscard]] result::Status submitInput(std::span<const std::byte> value);

    [[nodiscard]] ClientReplicationStatistics statistics() const noexcept;
    /// All zero without prediction.
    [[nodiscard]] PredictionStatistics predictionStatistics() const noexcept;
    /// The server tick remote entities are shown at, fractional; none
    /// without interpolation or before the first state.
    [[nodiscard]] std::optional<double> perceivedTick() const noexcept;
    /// All zero without interpolation.
    [[nodiscard]] InterpolationStatistics interpolationStatistics() const noexcept;

    struct State;
    explicit ReplicationClient(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_replication
