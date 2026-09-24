#include "rawframe/world_replication/server.h"

#include "rawframe/world/column_query.h"
#include "rawframe/world_replication/errors.h"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <span>

namespace rawframe::world_replication {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, ReplicationError error, std::string_view why) {
    return result::fail(errorClass, kReplicationDomain, code(error), why);
}

/// What one connection was sent of one entity's component. The value the
/// client holds is `lastSent` once it acknowledges any datagram from
/// `changedAt` on: every record from then carries it.
struct Replica {
    std::vector<std::byte> lastSent;
    bool sent = false;
    std::uint64_t changedAt = 0;
    std::optional<std::uint64_t> acknowledgedAt;

    [[nodiscard]] bool held(std::span<const std::byte> value) const noexcept {
        return sent && acknowledgedAt.has_value() && *acknowledgedAt >= changedAt &&
               std::ranges::equal(lastSent, value);
    }
};

struct Mapping {
    NetEntityId id;
    bool acknowledged = false;
    /// By replication table index.
    std::vector<Replica> replicas;
};

/// One state datagram sent and not yet known to have arrived.
struct SentState {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    bool acknowledged = false;
    std::vector<std::pair<world::EntityHandle, std::size_t>> records;
};

/// State datagrams remembered per connection for acknowledgement. One not
/// acknowledged by the time it leaves this window was lost, as far as the
/// server is concerned, and its values go again.
constexpr std::size_t kSentWindow = 256;
/// A state acknowledgement covers its newest sequence and the 64 before it.
constexpr std::uint64_t kAckBits = 64;

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
    std::deque<SentState> sent;
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

    // Scratch reused every tick. Every replicated value is encoded once per
    // tick, in entity then component order, and copied to each connection.
    struct PresentValue {
        world::EntityHandle entity;
        std::size_t component = 0;
        std::size_t offset = 0;
        std::size_t length = 0;
    };
    std::vector<PresentValue> present;
    std::vector<std::byte> encoded;
    std::vector<std::pair<world::EntityHandle, std::size_t>> inDatagram;
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

    void onStateAck(Peer& peer, const network::SessionEvent& event) {
        const auto kAck = decodeStateAck(event.payload);
        if (!kAck.has_value()) {
            ++statistics.acknowledgementsRefused;
            return;
        }
        // Only what this server sent can be acknowledged: a sequence it
        // never sent, or one already forgotten, finds nothing.
        for (auto sent = peer.sent.rbegin(); sent != peer.sent.rend(); ++sent) {
            if (sent->sequence > kAck->latest) {
                continue;
            }
            const std::uint64_t kBehind = kAck->latest - sent->sequence;
            if (kBehind > kAckBits) {
                break;
            }
            const bool kReceived = kBehind == 0 || ((kAck->earlier >> (kBehind - 1)) & 1U) != 0;
            if (!kReceived || sent->acknowledged) {
                continue;
            }
            sent->acknowledged = true;
            for (const auto& [entity, component] : sent->records) {
                const auto kMapping = peer.mapped.find(entity);
                if (kMapping == peer.mapped.end() || component >= kMapping->second.replicas.size()) {
                    continue;
                }
                Replica& replica = kMapping->second.replicas[component];
                replica.acknowledgedAt = std::max(replica.acknowledgedAt.value_or(0), sent->tick);
            }
        }
    }

    void onInput(Peer& peer, const network::SessionEvent& event) {
        if (event.laneEpoch == peer.accept.inputEpoch && event.payloadType == kStateAckPayload) {
            onStateAck(peer, event);
            return;
        }
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
        encoded.clear();
        for (std::size_t index = 0; index < queries.size(); ++index) {
            const ComponentCodec& codec = settings.table.components[index];
            const std::size_t kWire = codec.wireSize();
            queries[index].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    const std::size_t kOffset = encoded.size();
                    encoded.resize(kOffset + kWire);
                    network::Writer writer{std::span{encoded}.subspan(kOffset)};
                    if (!codec.encode(chunk.columns[0] + (row * codec.size), writer).has_value()) {
                        encoded.resize(kOffset);
                        continue;
                    }
                    present.push_back(PresentValue{
                        .entity = chunk.entities[row], .component = index, .offset = kOffset, .length = kWire});
                }
            });
        }
        std::sort(present.begin(), present.end(), [](const PresentValue& left, const PresentValue& right) {
            return left.entity != right.entity ? left.entity < right.entity : left.component < right.component;
        });
        for (auto& [id, peer] : peers) {
            publishTo(peer, tick);
        }
    }

    [[nodiscard]] bool isPresent(world::EntityHandle entity) const noexcept {
        const auto kFound = std::lower_bound(
            present.begin(), present.end(), entity, [](const PresentValue& value, world::EntityHandle key) {
                return value.entity < key;
            });
        return kFound != present.end() && kFound->entity == entity;
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
            if (isPresent(mapping->first)) {
                ++mapping;
                continue;
            }
            sendMapping(peer, network::ControlFrame::MappingRetire, mapping->second.id, false);
            peer.byNetEntity.erase(mapping->second.id.value);
            mapping = peer.mapped.erase(mapping);
        }
        // Declare what is new, before any state names it.
        for (std::size_t index = 0; index < present.size(); ++index) {
            const world::EntityHandle kEntity = present[index].entity;
            if ((index != 0 && present[index - 1].entity == kEntity) || peer.mapped.contains(kEntity) ||
                peer.mapped.size() >= settings.maximumMapped || peer.nextNetEntity == 0) {
                continue;
            }
            const NetEntityId kId{peer.nextNetEntity++};
            peer.mapped[kEntity] = Mapping{.id = kId};
            peer.byNetEntity[kId.value] = kEntity;
            sendMapping(peer, network::ControlFrame::MappingDeclare, kId, kEntity == peer.player);
        }
        // State for every acknowledged mapping, as many datagrams as it takes.
        const std::size_t kRoom = static_cast<std::size_t>(peer.accept.maximumDatagram);
        records.resize(kRoom);
        network::Writer recordWriter{records};
        std::uint64_t count = 0;
        std::size_t used = 0;
        inDatagram.clear();
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
                    peer.sent.push_back(SentState{
                        .sequence = peer.stateSequence, .tick = tick.value, .records = std::move(inDatagram)});
                    if (peer.sent.size() > kSentWindow) {
                        peer.sent.pop_front();
                    }
                }
            }
            count = 0;
            used = 0;
            recordWriter = network::Writer{records};
            inDatagram.clear();
        };
        Mapping* mapping = nullptr;
        for (std::size_t index = 0; index < present.size(); ++index) {
            const PresentValue& value = present[index];
            if (index == 0 || present[index - 1].entity != value.entity) {
                const auto kMapping = peer.mapped.find(value.entity);
                mapping = kMapping == peer.mapped.end() || !kMapping->second.acknowledged ? nullptr : &kMapping->second;
                if (mapping != nullptr) {
                    mapping->replicas.resize(settings.table.components.size());
                }
            }
            if (mapping == nullptr) {
                continue;
            }
            // What the client is known to hold already is not sent again.
            Replica& replica = mapping->replicas[value.component];
            const std::span<const std::byte> kValue = std::span{encoded}.subspan(value.offset, value.length);
            if (replica.held(kValue)) {
                ++statistics.recordsHeld;
                continue;
            }
            const std::size_t kRecord = 10 + value.length;
            if (used + kRecord + kStateHeaderRoom > kRoom) {
                kFlush();
            }
            if (!encodeStateRecordHead(recordWriter, {.entity = mapping->id, .component = value.component})
                     .has_value() ||
                !recordWriter.bytes(kValue).has_value()) {
                // Larger than a datagram on its own: it cannot be sent.
                recordWriter = network::Writer{records};
                count = 0;
                used = 0;
                inDatagram.clear();
                continue;
            }
            if (!replica.sent || !std::ranges::equal(replica.lastSent, kValue)) {
                replica.lastSent.assign(kValue.begin(), kValue.end());
                replica.changedAt = tick.value;
                replica.sent = true;
            }
            inDatagram.emplace_back(value.entity, value.component);
            used = recordWriter.written().size();
            ++count;
            ++statistics.recordsSent;
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
