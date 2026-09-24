#pragma once

// The authoritative side of replication (SPEC-0010 authority and tick model,
// SPEC-0041 input consumption). Each admitted connection gets a player entity;
// its input window is decoded outside the tick and consumed exactly once per
// input tick in `apply_inputs`; committed state is read in `replication` and
// sent as self-sufficient state datagrams for entities whose mapping the
// client has acknowledged. Interest is every replicated entity for now.

#include "rawframe/network/session.h"
#include "rawframe/result/result.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/records.h"
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

struct ServerReplicationSettings {
    ReplicationTable table;
    /// Components a player entity starts with, zeroed.
    std::vector<schema::ComponentTypeId> playerComponents;
    /// The component a connection's input commands are written into, one of
    /// the player's; none for a game without input.
    std::optional<ComponentCodec> input;
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
};

/// What the server counted.
struct ServerReplicationStatistics {
    std::uint64_t stateDatagrams = 0;
    std::uint64_t inputsConsumed = 0;
    std::uint64_t inputsHeld = 0;
    std::uint64_t inputsNeutral = 0;
    std::uint64_t inputsRefused = 0;
};

class ReplicationServer final : public world_runtime::SystemContributor {
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

    [[nodiscard]] ServerReplicationStatistics statistics() const noexcept;
    /// The player entity of an admitted connection, or the null handle.
    [[nodiscard]] world::EntityHandle player(network::ConnectionId connection) const noexcept;

    struct State;
    explicit ReplicationServer(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_replication
