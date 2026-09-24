#include "rawframe/world_replication/server.h"

#include "rawframe/world/column_query.h"
#include "rawframe/world_replication/errors.h"

#include <array>
#include <map>

namespace rawframe::world_replication {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, ReplicationError error, std::string_view why) {
    return result::fail(errorClass, kReplicationDomain, code(error), why);
}

struct Mapping {
    NetEntityId id;
    bool acknowledged = false;
};

/// One admitted connection.
struct Peer {
    network::ConnectionId connection;
    network::Accept accept;
    world::EntityHandle player;
    std::uint32_t nextNetEntity = 1;
    std::map<world::EntityHandle, Mapping> mapped;
    std::map<std::uint32_t, world::EntityHandle> byNetEntity;
    /// The next input tick to consume, and commands waiting for theirs.
    std::uint64_t nextInputTick = 0;
    std::uint64_t consumedInputTick = 0;
    std::map<std::uint64_t, std::vector<std::byte>> waitingInputs;
    std::vector<std::byte> lastCommand;
    std::uint32_t held = 0;
    std::uint64_t stateSequence = 0;
    /// How far ahead of consumption the newest command arrived, last seen.
    std::int64_t measuredLead = 0;
    bool heardInput = false;
};

/// Bytes a state datagram's framing takes around its payload, at most.
constexpr std::size_t kStateHeaderRoom = 3 * 8;

} // namespace

struct ReplicationServer::State {
    network::Sessions* sessions = nullptr;
    ServerReplicationSettings settings;
    std::map<std::uint64_t, Peer> peers;
    ServerReplicationStatistics statistics;
    std::vector<network::SessionEvent> events;

    // Resolved once the registry is frozen.
    std::vector<schema::ComponentRuntimeId> table;
    std::vector<schema::ComponentRuntimeId> playerComponents;
    std::optional<schema::ComponentRuntimeId> input;
    std::vector<world::ColumnQuery> queries;
    std::vector<std::unique_ptr<world::System>> systems;
    std::vector<schema::ComponentRuntimeId> inputWrites;

    // Scratch reused every tick.
    std::map<world::EntityHandle, std::vector<std::pair<std::size_t, const std::byte*>>> present;
    std::vector<std::byte> records;
    std::vector<std::byte> datagram;
    std::vector<std::byte> frame;

    void sendMapping(Peer& peer, network::ControlFrame type, NetEntityId entity, bool owned) {
        frame.resize(32);
        network::Writer writer{frame};
        if (encodeMapping(
                writer,
                MappingRecord{.replicationEpoch = peer.accept.replicationEpoch, .entity = entity, .owned = owned})
                .has_value()) {
            static_cast<void>(sessions->sendFrame(peer.connection, type, writer.written()));
        }
    }

    void onFrame(world::World& world, Peer& peer, const network::SessionEvent& event) {
        const auto kType = static_cast<network::ControlFrame>(event.frameType);
        if (kType == network::ControlFrame::MappingAck || kType == network::ControlFrame::MappingRetireAck) {
            const auto kRecord = decodeMapping(event.payload);
            if (!kRecord.has_value() || kRecord->replicationEpoch != peer.accept.replicationEpoch) {
                return;
            }
            if (kType == network::ControlFrame::MappingAck) {
                const auto kEntity = peer.byNetEntity.find(kRecord->entity.value);
                if (kEntity != peer.byNetEntity.end()) {
                    peer.mapped[kEntity->second].acknowledged = true;
                }
            }
            return;
        }
        if (kType == network::ControlFrame::StateAck || kType == network::ControlFrame::Heartbeat ||
            kType == network::ControlFrame::GracefulClose) {
            return;
        }
        // A client sending what only a server sends is not playing by the
        // protocol.
        static_cast<void>(world);
        sessions->close(peer.connection);
    }

    void onInput(Peer& peer, const network::SessionEvent& event) {
        if (!settings.input || event.laneEpoch != peer.accept.inputEpoch || event.payloadType != kInputWindowPayload) {
            ++statistics.inputsRefused;
            return;
        }
        const auto kWindow = decodeInputWindow(event.payload);
        if (!kWindow.has_value()) {
            ++statistics.inputsRefused;
            return;
        }
        peer.measuredLead =
            static_cast<std::int64_t>(kWindow->newestInputTick) - static_cast<std::int64_t>(peer.nextInputTick);
        peer.heardInput = true;
        const std::uint64_t kFirst = kWindow->newestInputTick + 1 - kWindow->commands.size();
        for (std::size_t index = 0; index < kWindow->commands.size(); ++index) {
            const std::uint64_t kTick = kFirst + index;
            const auto kCommand = kWindow->commands[index];
            if (kTick < peer.nextInputTick) {
                continue; // already consumed: redundancy, not an error
            }
            if (kTick >= peer.nextInputTick + settings.inputFutureWindow ||
                kCommand.size() != settings.input->wireSize()) {
                ++statistics.inputsRefused;
                continue;
            }
            peer.waitingInputs.try_emplace(kTick, kCommand.begin(), kCommand.end());
        }
    }

    void applyInputs(world::World& world) {
        if (!settings.input || !input) {
            return;
        }
        const std::size_t kWire = settings.input->wireSize();
        for (auto& [id, peer] : peers) {
            std::byte* const kValue = static_cast<std::byte*>(world.getErased(peer.player, *input));
            if (kValue == nullptr) {
                continue;
            }
            std::vector<std::byte> command;
            const auto kWaiting = peer.waitingInputs.find(peer.nextInputTick);
            if (kWaiting != peer.waitingInputs.end()) {
                command = std::move(kWaiting->second);
                peer.lastCommand = command;
                peer.held = 0;
                ++statistics.inputsConsumed;
            } else if (!peer.lastCommand.empty() && peer.held < settings.inputHoldLast) {
                command = peer.lastCommand;
                ++peer.held;
                ++statistics.inputsHeld;
            } else {
                command.assign(kWire, std::byte{0});
                ++statistics.inputsNeutral;
            }
            peer.waitingInputs.erase(peer.waitingInputs.begin(), peer.waitingInputs.upper_bound(peer.nextInputTick));
            // Held or neutral, the tick is consumed all the same: a command
            // for it arriving later is too late (SPEC-0041).
            peer.consumedInputTick = peer.nextInputTick++;
            network::Reader reader{command};
            static_cast<void>(settings.input->decode(reader, kValue));
        }
    }

    void publish(world::World& world, world::TickIndex tick) {
        present.clear();
        for (std::size_t index = 0; index < queries.size(); ++index) {
            const std::size_t kSize = settings.table.components[index].size;
            queries[index].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    present[chunk.entities[row]].emplace_back(index, chunk.columns[0] + (row * kSize));
                }
            });
        }
        for (auto& [id, peer] : peers) {
            publishTo(peer, tick);
        }
    }

    void sendPace(Peer& peer) {
        std::array<std::byte, 24> payload{};
        network::Writer writer{payload};
        if (encodePace(writer, Pace{.measuredLead = peer.measuredLead, .targetLead = settings.targetInputLead})
                .has_value()) {
            static_cast<void>(sessions->sendDatagram(peer.connection,
                                                     network::DatagramRecord{.lane = network::DatagramLane::State,
                                                                             .laneEpoch = peer.accept.replicationEpoch,
                                                                             .sequence = ++peer.stateSequence,
                                                                             .payloadType = kPacePayload,
                                                                             .payload = writer.written()}));
        }
    }

    void publishTo(Peer& peer, world::TickIndex tick) {
        if (settings.input && peer.heardInput && tick.value % settings.paceInterval == 0) {
            sendPace(peer);
        }
        // Retire what is gone; the ID is never used again in this epoch.
        for (auto mapping = peer.mapped.begin(); mapping != peer.mapped.end();) {
            if (present.contains(mapping->first)) {
                ++mapping;
                continue;
            }
            sendMapping(peer, network::ControlFrame::MappingRetire, mapping->second.id, false);
            peer.byNetEntity.erase(mapping->second.id.value);
            mapping = peer.mapped.erase(mapping);
        }
        // Declare what is new, before any state names it.
        for (const auto& [entity, values] : present) {
            if (peer.mapped.contains(entity) || peer.mapped.size() >= settings.maximumMapped ||
                peer.nextNetEntity == 0) {
                continue;
            }
            const NetEntityId kId{peer.nextNetEntity++};
            peer.mapped[entity] = Mapping{.id = kId};
            peer.byNetEntity[kId.value] = entity;
            sendMapping(peer, network::ControlFrame::MappingDeclare, kId, entity == peer.player);
        }
        // State for every acknowledged mapping, as many datagrams as it takes.
        const std::size_t kRoom = static_cast<std::size_t>(peer.accept.maximumDatagram);
        records.resize(kRoom);
        network::Writer recordWriter{records};
        std::uint64_t count = 0;
        std::size_t used = 0;
        const auto kFlush = [&] {
            if (count == 0) {
                return;
            }
            datagram.resize(kRoom + kStateHeaderRoom);
            network::Writer writer{datagram};
            if (encodeStateHeader(writer,
                                  StateHeader{.serverTick = tick.value,
                                              .consumedInputTick = peer.consumedInputTick,
                                              .recordCount = count})
                    .has_value() &&
                writer.bytes(std::span{records}.first(used)).has_value() && writer.written().size() <= kRoom) {
                const network::DatagramRecord kState{.lane = network::DatagramLane::State,
                                                     .laneEpoch = peer.accept.replicationEpoch,
                                                     .sequence = ++peer.stateSequence,
                                                     .payloadType = kStatePayload,
                                                     .payload = writer.written()};
                if (sessions->sendDatagram(peer.connection, kState).has_value()) {
                    ++statistics.stateDatagrams;
                }
            }
            count = 0;
            used = 0;
            recordWriter = network::Writer{records};
        };
        for (const auto& [entity, values] : present) {
            const auto kMapping = peer.mapped.find(entity);
            if (kMapping == peer.mapped.end() || !kMapping->second.acknowledged) {
                continue;
            }
            for (const auto& [index, value] : values) {
                const ComponentCodec& codec = settings.table.components[index];
                const std::size_t kRecord = 10 + codec.wireSize();
                if (used + kRecord + kStateHeaderRoom > kRoom) {
                    kFlush();
                }
                if (!encodeStateRecordHead(recordWriter, {.entity = kMapping->second.id, .component = index})
                         .has_value() ||
                    !codec.encode(value, recordWriter).has_value()) {
                    // Larger than a datagram on its own: it cannot be sent.
                    recordWriter = network::Writer{records};
                    count = 0;
                    used = 0;
                    continue;
                }
                used = recordWriter.written().size();
                ++count;
            }
        }
        kFlush();
    }
};

namespace {

/// `rawframe.replication.apply_inputs`: each connection's next input into its
/// player's input component.
class ApplyInputs final : public world::System {
public:
    explicit ApplyInputs(ReplicationServer::State& state) noexcept : state_(&state) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        state_->applyInputs(context.world);
        return {};
    }

private:
    ReplicationServer::State* state_;
};

/// `rawframe.replication.publish`: committed state out to every connection.
class Publish final : public world::System {
public:
    explicit Publish(ReplicationServer::State& state) noexcept : state_(&state) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        state_->publish(context.world, context.tick);
        return {};
    }

private:
    ReplicationServer::State* state_;
};

} // namespace

ReplicationServer::ReplicationServer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ReplicationServer::~ReplicationServer() = default;

result::Result<std::unique_ptr<ReplicationServer>> ReplicationServer::create(network::Sessions& sessions,
                                                                             ServerReplicationSettings settings) {
    for (const ComponentCodec& codec : settings.table.components) {
        if (!codec.valid()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "a replicated component's field lies outside its value");
        }
    }
    if ((settings.input && !settings.input->valid()) || settings.maximumMapped == 0 ||
        settings.inputFutureWindow == 0 || settings.paceInterval == 0 ||
        settings.targetInputLead >= settings.inputFutureWindow) {
        return refuse(result::ErrorClass::InvalidArgument,
                      ReplicationError::Malformed,
                      "the input codec is invalid, or a replication bound is zero");
    }
    auto state = std::make_unique<State>();
    state->sessions = &sessions;
    state->settings = std::move(settings);
    return std::make_unique<ReplicationServer>(std::move(state));
}

result::Status ReplicationServer::declareSystems(const schema::SchemaRegistry& registry,
                                                 std::vector<world::SystemDeclaration>& systems) noexcept {
    State& state = *state_;
    state.table.clear();
    state.queries.clear();
    state.playerComponents.clear();
    state.inputWrites.clear();
    state.input.reset();
    for (const ComponentCodec& codec : state.settings.table.components) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(codec.component));
        const schema::ComponentDescriptor& descriptor = registry.descriptor(kId);
        if (!descriptor.plainData || descriptor.size != codec.size) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "a replicated component is not plain data of its codec's size");
        }
        state.table.push_back(kId);
        const std::array<world::ColumnTerm, 1> kTerm = {world::ColumnTerm{kId, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerm, registry));
        state.queries.push_back(std::move(query));
    }
    for (const schema::ComponentTypeId kComponent : state.settings.playerComponents) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(kComponent));
        state.playerComponents.push_back(kId);
    }
    if (state.settings.input) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(state.settings.input->component));
        if (registry.descriptor(kId).size != state.settings.input->size) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "the input component is not its codec's size");
        }
        state.input = kId;
        state.inputWrites.push_back(kId);
    }
    state.systems.clear();
    state.systems.push_back(std::make_unique<ApplyInputs>(state));
    state.systems.push_back(std::make_unique<Publish>(state));
    systems.push_back(world::SystemDeclaration{.identity = "rawframe.replication.apply_inputs",
                                               .phase = world::Phase::ApplyInputs,
                                               .writes = state.inputWrites,
                                               .system = state.systems[0].get()});
    systems.push_back(world::SystemDeclaration{.identity = "rawframe.replication.publish",
                                               .phase = world::Phase::Replication,
                                               .reads = state.table,
                                               .system = state.systems[1].get()});
    return {};
}

void ReplicationServer::pump(world::World& world, world::TickIndex tick) {
    State& state = *state_;
    state.sessions->setTickOrigin(tick.value);
    state.events.clear();
    state.sessions->pump(state.events);
    for (network::SessionEvent& event : state.events) {
        const auto kPeer = state.peers.find(event.connection.value);
        switch (event.kind) {
        case network::SessionEventKind::Admitted: {
            // No system runs between ticks, so the player is made directly.
            auto player = world.create();
            bool made = player.has_value();
            for (const schema::ComponentRuntimeId kId : state.playerComponents) {
                if (!made) {
                    break;
                }
                std::vector<std::byte> zero(world.registry().descriptor(kId).size);
                made = world.insertErased(*player, kId, zero.data()).has_value();
            }
            if (!made) {
                if (player.has_value()) {
                    static_cast<void>(world.destroy(*player));
                }
                state.sessions->close(event.connection);
                break;
            }
            // Input ticks start after the origin, so "consumed through the
            // origin" names nothing consumed yet.
            state.peers[event.connection.value] = Peer{.connection = event.connection,
                                                       .accept = event.accept,
                                                       .player = *player,
                                                       .nextInputTick = event.accept.tickOrigin + 1,
                                                       .consumedInputTick = event.accept.tickOrigin};
            break;
        }
        case network::SessionEventKind::Frame:
            if (kPeer != state.peers.end()) {
                state.onFrame(world, kPeer->second, event);
            }
            break;
        case network::SessionEventKind::Datagram:
            if (kPeer != state.peers.end()) {
                state.onInput(kPeer->second, event);
            }
            break;
        case network::SessionEventKind::Ended:
            if (kPeer != state.peers.end()) {
                static_cast<void>(world.destroy(kPeer->second.player));
                state.peers.erase(kPeer);
            }
            break;
        case network::SessionEventKind::Rejected:
            break;
        }
    }
}

ServerReplicationStatistics ReplicationServer::statistics() const noexcept {
    return state_->statistics;
}

world::EntityHandle ReplicationServer::player(network::ConnectionId connection) const noexcept {
    const auto kPeer = state_->peers.find(connection.value);
    return kPeer == state_->peers.end() ? world::EntityHandle{} : kPeer->second.player;
}

} // namespace rawframe::world_replication
