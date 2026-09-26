#include "rawframe/world_replication/server.h"

#include "rawframe/world_replication/checksum.h"
#include "server_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <span>

namespace rawframe::world_replication {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, ReplicationError error, std::string_view why) {
    return result::fail(errorClass, kReplicationDomain, code(error), why);
}

} // namespace

void ReplicationServer::State::sendMapping(Peer& peer, network::ControlFrame type, NetEntityId entity, bool owned) {
    frame.resize(32);
    network::Writer writer{frame};
    if (encodeMapping(writer,
                      MappingRecord{.replicationEpoch = peer.accept.replicationEpoch, .entity = entity, .owned = owned})
            .has_value()) {
        static_cast<void>(sessions->sendFrame(peer.connection, type, writer.written()));
    }
}

void ReplicationServer::State::onFrame(world::World& world, Peer& peer, const network::SessionEvent& event) {
    const auto kType = static_cast<network::ControlFrame>(event.frameType);
    if (kType == network::ControlFrame::MappingAck || kType == network::ControlFrame::MappingRetireAck) {
        const auto kRecord = decodeMapping(event.payload);
        if (!kRecord.has_value() || kRecord->replicationEpoch != peer.accept.replicationEpoch) {
            return;
        }
        if (kType == network::ControlFrame::MappingAck) {
            const auto kEntity = peer.byNetEntity.find(kRecord->entity.value);
            Mapping* const kMapping = kEntity != peer.byNetEntity.end() ? peer.mapped.find(kEntity->second) : nullptr;
            if (kMapping != nullptr) {
                kMapping->acknowledged = true;
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

void ReplicationServer::State::onStateAck(Peer& peer, const network::SessionEvent& event) {
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
        for (const auto& [id, component] : sent->records) {
            const auto kEntity = peer.byNetEntity.find(id);
            if (kEntity == peer.byNetEntity.end()) {
                continue;
            }
            Mapping* const kMapping = peer.mapped.find(kEntity->second);
            if (kMapping == nullptr || component >= kMapping->replicas.size()) {
                continue;
            }
            Replica& replica = kMapping->replicas[component];
            replica.acknowledgedAt = std::max(replica.acknowledgedAt.value_or(0), sent->tick);
        }
    }
    // What lies more than an acknowledgement's reach behind the newest one
    // heard can never be acknowledged: forgotten now rather than when the
    // window fills (D217). A connection claiming more than was sent only
    // forgets what it could have acknowledged.
    const std::uint64_t kLatest = std::min(kAck->latest, peer.stateSequence);
    while (!peer.sent.empty() && peer.sent.front().sequence + kAckBits < kLatest) {
        peer.sent.pop_front();
    }
}

void ReplicationServer::State::onInput(Peer& peer, const network::SessionEvent& event) {
    if (event.laneEpoch == peer.accept.inputEpoch && event.payloadType == kStateAckPayload) {
        onStateAck(peer, event);
        return;
    }
    if (event.laneEpoch == peer.accept.inputEpoch && event.payloadType == kChecksumPayload) {
        onChecksum(peer, event);
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
        const std::size_t kWire = settings.input->wireSize();
        const bool kShaped = settings.perception
                                 ? kCommand.size() > kWire && kCommand.size() <= kWire + kMaximumPerceptionBytes &&
                                       decodePerception(kCommand.subspan(kWire)).has_value()
                                 : kCommand.size() == kWire;
        if (kTick >= peer.nextInputTick + settings.inputFutureWindow || !kShaped) {
            ++statistics.inputsRefused;
            continue;
        }
        peer.waitingInputs.try_emplace(kTick,
                                       Waiting{.command = {kCommand.begin(), kCommand.end()}, .arrived = pumpTick});
    }
}

void ReplicationServer::State::applyInputs(world::World& world) {
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
            command = std::move(kWaiting->second.command);
            if (perception) {
                peer.seen = perceived(peer, command, kWire, kWaiting->second.arrived);
            }
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
            // A neutral command saw nothing.
            peer.seen = Perception{.viewer = peer.viewer};
        }
        if (perception) {
            // A held command keeps the moment of the one it repeats.
            auto* const kInto = static_cast<Perception*>(world.getErased(peer.player, *perception));
            if (kInto != nullptr) {
                *kInto = peer.seen;
            }
        }
        peer.waitingInputs.erase(peer.waitingInputs.begin(), peer.waitingInputs.upper_bound(peer.nextInputTick));
        // Held or neutral, the tick is consumed all the same: a command
        // for it arriving later is too late (SPEC-0041).
        peer.consumedInputTick = peer.nextInputTick++;
        network::Reader reader{command};
        const PeerNames kNames{peer};
        static_cast<void>(settings.input->decode(reader, kValue, &kNames));
    }
}

Perception ReplicationServer::State::perceived(Peer& peer,
                                               std::span<const std::byte> command,
                                               std::size_t wire,
                                               std::uint64_t arrived) {
    const auto kSeen = decodePerception(command.subspan(wire));
    if (!kSeen.has_value()) {
        return Perception{.viewer = peer.viewer};
    }
    const KeptPerception kKept = keepPerception(*kSeen, arrived, settings.perceptionSkew, peer.lag);
    statistics.perceptionsClamped += kKept.clamped ? 1 : 0;
    return Perception{.baseTick = kKept.moment.baseTick, .fraction = kKept.moment.fraction, .viewer = peer.viewer};
}

void ReplicationServer::State::onChecksum(Peer& peer, const network::SessionEvent& event) {
    const std::uint64_t kSeconds = std::max<std::uint64_t>(peer.accept.tickRateSeconds, 1);
    if (!peer.checksums.admit(
            pumpTick, (peer.accept.tickRateTicks + kSeconds - 1) / kSeconds, settings.checksumsPerSecond)) {
        ++statistics.checksumsLimited;
        return;
    }
    const auto kRecord = decodeChecksum(event.payload);
    if (!kRecord.has_value()) {
        ++statistics.inputsRefused;
        return;
    }
    const std::optional<std::uint64_t> kExpected = peer.checksums.at(kRecord->tick);
    const ChecksumBook::Verdict kVerdict = kRecord->scope != scope || predicted.empty()
                                               ? ChecksumBook::Verdict::Unverifiable
                                               : peer.checksums.check(kRecord->tick, kRecord->checksum);
    statistics.checksumsVerified += kVerdict == ChecksumBook::Verdict::Verified ? 1 : 0;
    statistics.checksumsUnverifiable += kVerdict == ChecksumBook::Verdict::Unverifiable ? 1 : 0;
    if (kVerdict == ChecksumBook::Verdict::Diverged) {
        ++statistics.checksumsDiverged;
        if (divergences.size() < 64) {
            divergences.push_back(Divergence{.connection = peer.connection,
                                             .tick = kRecord->tick,
                                             .scope = kRecord->scope,
                                             .expected = *kExpected,
                                             .received = kRecord->checksum});
        }
    }
}

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
    if ((settings.input && !settings.input->valid()) || (settings.perception && !settings.input) ||
        settings.maximumMapped == 0 || settings.inputFutureWindow == 0 || settings.paceInterval == 0 ||
        settings.targetInputLead >= settings.inputFutureWindow) {
        return refuse(result::ErrorClass::InvalidArgument,
                      ReplicationError::Malformed,
                      "the input codec is invalid, or a replication bound is zero");
    }
    if (settings.interest) {
        const InterestSettings& interest = *settings.interest;
        const bool kAxes = !interest.axes.empty() && interest.axes.size() <= 3 &&
                           std::ranges::all_of(interest.axes, [](const WireField& field) {
                               return field.kind == WireKind::F32 || field.kind == WireKind::F64;
                           });
        // Written so that a radius that is not a number fails too.
        if (!kAxes || !(interest.radius > 0) || !(interest.leaveRadius >= interest.radius) ||
            !std::isfinite(interest.leaveRadius)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::Malformed,
                          "interest takes one to three floating-point axes, a positive radius, and a leaving radius "
                          "no smaller");
        }
    }
    auto state = std::make_unique<State>();
    state->sessions = &sessions;
    state->settings = std::move(settings);
    for (const ComponentCodec& codec : state->settings.table.components) {
        state->naming.push_back(codec.namesEntities() ? 1 : 0);
    }
    for (const schema::ComponentTypeId& component : state->settings.predicted) {
        const auto& kTable = state->settings.table.components;
        const auto kCodec = std::ranges::find(kTable, component, &ComponentCodec::component);
        if (kCodec == kTable.end()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::Malformed,
                          "a predicted component is not replicated");
        }
        state->predicted.push_back(static_cast<std::size_t>(kCodec - kTable.begin()));
    }
    state->scope = scopeFingerprint(state->settings.predicted);
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
    state.perception.reset();
    state.positions.reset();
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
        if (state.settings.perception) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kSeen, registry.find(Perception::kComponentTypeId));
            if (registry.descriptor(kSeen).size != sizeof(Perception)) {
                return refuse(result::ErrorClass::InvalidArgument,
                              ReplicationError::FieldUnsupported,
                              "the perception component is not the engine's");
            }
            state.perception = kSeen;
            state.inputWrites.push_back(kSeen);
        }
    }
    state.reads = state.table;
    if (state.settings.interest) {
        const InterestSettings& interest = *state.settings.interest;
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(interest.position));
        const std::size_t kSize = registry.descriptor(kId).size;
        state.positionSize = kSize;
        if (!std::ranges::all_of(interest.axes, [&](const WireField& field) {
                return field.offset <= kSize && widthOf(field.kind) <= kSize - field.offset;
            })) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "an interest axis lies outside the position component");
        }
        const std::array<world::ColumnTerm, 1> kTerm = {world::ColumnTerm{kId, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(state.positions, world::ColumnQuery::resolve(kTerm, registry));
        if (std::ranges::find(state.reads, kId) == state.reads.end()) {
            state.reads.push_back(kId);
        }
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
                                               .reads = state.reads,
                                               .system = state.systems[1].get()});
    return {};
}

void ReplicationServer::forgetWorld() noexcept {
    State& state = *state_;
    for (auto& [id, peer] : state.peers) {
        state.sessions->close(peer.connection);
    }
    state.peers.clear();
}

void ReplicationServer::pump(world::World& world, world::TickIndex tick) {
    State& state = *state_;
    state.pumpTick = tick.value;
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
            const auto kIdentity = world_runtime::playerIdentity(event.requestedSession);
            state.peers[event.connection.value] = Peer{.connection = event.connection,
                                                       .accept = event.accept,
                                                       .player = *player,
                                                       .identity = kIdentity,
                                                       .viewer = ++state.lastViewer,
                                                       .nextInputTick = event.accept.tickOrigin + 1,
                                                       .consumedInputTick = event.accept.tickOrigin};
            if (kIdentity.has_value() && state.settings.presence != nullptr) {
                state.settings.presence->joined(world, *player, *kIdentity);
            }
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
                if (kPeer->second.identity.has_value() && state.settings.presence != nullptr) {
                    state.settings.presence->leaving(world, kPeer->second.player, *kPeer->second.identity);
                }
                static_cast<void>(world.destroy(kPeer->second.player));
                state.peers.erase(kPeer);
            }
            break;
        case network::SessionEventKind::Rejected:
            break;
        }
    }
}

std::optional<std::uint64_t>
ReplicationServer::sentSince(std::uint32_t viewer, world::EntityHandle entity, std::uint64_t tick) const noexcept {
    for (const auto& [id, peer] : state_->peers) {
        if (peer.viewer != viewer) {
            continue;
        }
        if (entity == peer.player) {
            return 0;
        }
        const Mapping* const kMapped = peer.mapped.find(entity);
        if (kMapped != nullptr && kMapped->firstSent.has_value()) {
            return kMapped->firstSent;
        }
        const auto kThen = std::ranges::find_if(peer.retired, [&](const Sent& gone) {
            return gone.entity == entity && gone.from <= tick && tick < gone.until;
        });
        return kThen != peer.retired.end() ? std::optional{kThen->from} : std::nullopt;
    }
    return std::nullopt;
}

ServerReplicationStatistics ReplicationServer::statistics() const noexcept {
    return state_->statistics;
}

namespace {

template <typename Vector> std::size_t capacityBytes(const Vector& vector) noexcept {
    return vector.capacity() * sizeof(typename Vector::value_type);
}

/// What a map's node holds beside its entry: three links and a color.
constexpr std::size_t kNodeOverhead = 32;

} // namespace

std::size_t ReplicationServer::heldBytes() const noexcept {
    const State& state = *state_;
    std::size_t bytes = capacityBytes(state.present) + capacityBytes(state.gathered) + capacityBytes(state.encoded) +
                        capacityBytes(state.named) + capacityBytes(state.valuesAt) + capacityBytes(state.filling) +
                        capacityBytes(state.located) + capacityBytes(state.locatedAt) + capacityBytes(state.entities) +
                        capacityBytes(state.entityAt) + capacityBytes(state.bucketed) + capacityBytes(state.bucketAt) +
                        capacityBytes(state.bucketOf) + capacityBytes(state.unplaced) + capacityBytes(state.reachable) +
                        capacityBytes(state.entering) + capacityBytes(state.heldMark) +
                        capacityBytes(state.candidates) + capacityBytes(state.records) + capacityBytes(state.datagram) +
                        capacityBytes(state.frame) + capacityBytes(state.inDatagram);
    for (const auto& [id, peer] : state.peers) {
        bytes += sizeof(Peer) + kNodeOverhead + capacityBytes(peer.retired) + capacityBytes(peer.lastCommand) +
                 (peer.byNetEntity.size() * (sizeof(std::pair<std::uint32_t, world::EntityHandle>) + kNodeOverhead));
        bytes += peer.mapped.capacity() * sizeof(Mappings::Entry);
        for (const auto& [entity, mapping] : peer.mapped) {
            bytes += capacityBytes(mapping.replicas);
            for (const Replica& replica : mapping.replicas) {
                bytes += replica.lastSent.heapBytes();
            }
        }
        for (const auto& [tick, waiting] : peer.waitingInputs) {
            bytes += sizeof(Waiting) + kNodeOverhead + capacityBytes(waiting.command);
        }
        for (const SentState& sent : peer.sent) {
            bytes += sizeof(SentState) + capacityBytes(sent.records);
        }
    }
    return bytes;
}

std::size_t ReplicationServer::connections() const noexcept {
    return state_->peers.size();
}

bool ReplicationServer::playing(world_runtime::PlayerIdentity identity) const noexcept {
    return std::ranges::any_of(state_->peers, [identity](const auto& entry) {
        return entry.second.identity == identity;
    });
}

void ReplicationServer::leaveAll(world::World& world) noexcept {
    if (state_->settings.presence == nullptr) {
        return;
    }
    for (const auto& [id, peer] : state_->peers) {
        if (peer.identity.has_value() && world.alive(peer.player)) {
            state_->settings.presence->leaving(world, peer.player, *peer.identity);
        }
    }
}

void ReplicationServer::noticeStopping() noexcept {
    std::array<std::byte, 8> bytes{};
    network::Writer writer{bytes};
    if (!network::encodeGracefulClose(writer, network::CloseNotice::ServerStopping).has_value()) {
        return;
    }
    for (const auto& [id, peer] : state_->peers) {
        static_cast<void>(
            state_->sessions->sendFrame(peer.connection, network::ControlFrame::GracefulClose, writer.written()));
    }
}

world::EntityHandle ReplicationServer::player(network::ConnectionId connection) const noexcept {
    const auto kPeer = state_->peers.find(connection.value);
    return kPeer == state_->peers.end() ? world::EntityHandle{} : kPeer->second.player;
}

std::vector<Divergence> ReplicationServer::takeDivergences() {
    return std::exchange(state_->divergences, {});
}

std::uint64_t ReplicationServer::divergences(network::ConnectionId connection) const noexcept {
    const auto kPeer = state_->peers.find(connection.value);
    return kPeer == state_->peers.end() ? 0 : kPeer->second.checksums.divergences;
}

} // namespace rawframe::world_replication
