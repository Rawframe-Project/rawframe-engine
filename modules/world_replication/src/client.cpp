#include "rawframe/world_replication/client.h"

#include "interpolation.h"
#include "prediction.h"
#include "rawframe/network/close.h"
#include "rawframe/world_replication/checksum.h"
#include "rawframe/world_replication/errors.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace rawframe::world_replication {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, ReplicationError error, std::string_view why) {
    return result::fail(errorClass, kReplicationDomain, code(error), why);
}

/// SPEC-0041's client time dilation (D317): input more than `kLeadSlack`
/// ticks ahead of the target lead is brought back by leaving one sample in
/// `kDilationSamples` unlabelled (`client_dilation_max`, a fifth).
constexpr std::int64_t kLeadSlack = 2;
constexpr std::uint64_t kDilationSamples = 5;

struct Staged {
    std::uint32_t net = 0;
    world::EntityHandle entity;
    std::size_t component = 0;
    std::size_t offset = 0;
};

/// Entities by the IDs this client mirrors them under.
class MirrorNames final : public EntityNames {
public:
    explicit MirrorNames(const std::map<std::uint32_t, world::EntityHandle>& mirrored) noexcept : mirrored_(&mirrored) {
    }
    [[nodiscard]] std::uint32_t netOf(world::EntityHandle entity) const noexcept override {
        for (const auto& [kNet, kEntity] : *mirrored_) {
            if (kEntity == entity) {
                return kNet;
            }
        }
        return 0;
    }
    [[nodiscard]] world::EntityHandle entityOf(std::uint32_t net) const noexcept override {
        const auto kFound = mirrored_->find(net);
        return kFound != mirrored_->end() ? kFound->second : world::EntityHandle{};
    }

private:
    const std::map<std::uint32_t, world::EntityHandle>* mirrored_;
};

} // namespace

struct ReplicationClient::State {
    network::Sessions* sessions = nullptr;
    world::World* world = nullptr;
    ClientReplicationSettings settings;
    std::vector<schema::ComponentRuntimeId> table;
    ClientReplicationStatistics statistics;

    std::optional<network::ConnectionId> connection;
    std::optional<network::Accept> accept;
    std::optional<network::RejectReason> rejection;
    bool serverStopping = false;
    std::optional<network::Termination> termination;
    bool ended = false;
    std::map<std::uint32_t, world::EntityHandle> mirrored;
    MirrorNames names{mirrored};
    /// The server tick each entity's component was last applied at.
    std::map<std::pair<std::uint32_t, std::size_t>, std::uint64_t> appliedAt;
    world::EntityHandle owned;
    std::uint32_t ownedNet = 0;
    std::optional<Prediction> prediction;
    std::optional<Interpolation> interpolation;
    /// Table indices of the neighborhood components, and scratch.
    std::vector<std::size_t> neighborhood;
    std::vector<NeighborValue> neighborValues;
    /// By table index: which predicted component it is, if any.
    std::vector<std::optional<std::size_t>> predictedIndex;
    /// Each predicted component's table index, and the scope's fingerprint.
    std::vector<std::size_t> predictedTable;
    std::uint64_t scope = 0;
    std::uint64_t confirmations = 0;
    std::uint64_t checksumsSent = 0;
    /// The latest checksum records sent, for a development capture.
    std::deque<ChecksumCapture> captures;
    std::uint64_t serverTick = 0;
    std::uint64_t stateSequence = 0;
    std::uint64_t consumedInputTick = 0;
    /// State datagrams received, for the next acknowledgement.
    StateAck received;
    bool acknowledgementDue = false;

    // Input: commands for consecutive input ticks not yet consumed.
    std::uint64_t nextInputTick = 0;
    std::uint64_t paceSequence = 0;
    /// Samples still to leave unlabelled, and those labelled since the last.
    std::uint64_t holdBack = 0;
    std::uint64_t sinceHeld = 0;
    /// Whether the next pace measured mostly before the last jump ahead.
    bool jumped = false;
    std::map<std::uint64_t, std::vector<std::byte>> unconsumed;
    std::uint64_t inputSequence = 0;

    std::vector<network::SessionEvent> events;
    std::vector<std::byte> staging;
    std::vector<Staged> staged;
    std::vector<std::byte> scratch;

    void acknowledge(network::ControlFrame type, NetEntityId entity) {
        scratch.resize(32);
        network::Writer writer{scratch};
        if (encodeMapping(writer, MappingRecord{.replicationEpoch = accept->replicationEpoch, .entity = entity})
                .has_value()) {
            static_cast<void>(sessions->sendFrame(*connection, type, writer.written()));
        }
    }

    /// The session is over: nothing mirrored stays, and a new one starts
    /// from nothing.
    void end() {
        ended = true;
        for (const auto& [id, entity] : mirrored) {
            static_cast<void>(world->destroy(entity));
        }
        mirrored.clear();
        appliedAt.clear();
        owned = {};
        ownedNet = 0;
        if (prediction) {
            prediction->reset();
        }
        if (interpolation) {
            interpolation->reset();
        }
        accept.reset();
        received = {};
        acknowledgementDue = false;
    }

    void onFrame(const network::SessionEvent& event) {
        const auto kType = static_cast<network::ControlFrame>(event.frameType);
        if (kType == network::ControlFrame::GracefulClose) {
            serverStopping =
                serverStopping || network::decodeGracefulClose(event.payload) == network::CloseNotice::ServerStopping;
            return;
        }
        if (kType != network::ControlFrame::MappingDeclare && kType != network::ControlFrame::MappingRetire) {
            return;
        }
        const auto kRecord = decodeMapping(event.payload);
        if (!kRecord.has_value() || kRecord->replicationEpoch != accept->replicationEpoch) {
            return;
        }
        if (kType == network::ControlFrame::MappingDeclare) {
            if (mirrored.contains(kRecord->entity.value) || mirrored.size() >= settings.maximumMapped) {
                sessions->close(*connection);
                return;
            }
            auto entity = world->create();
            if (!entity.has_value()) {
                sessions->close(*connection);
                return;
            }
            mirrored[kRecord->entity.value] = *entity;
            if (kRecord->owned) {
                owned = *entity;
                ownedNet = kRecord->entity.value;
            }
            acknowledge(network::ControlFrame::MappingAck, kRecord->entity);
            return;
        }
        const auto kMirror = mirrored.find(kRecord->entity.value);
        if (kMirror != mirrored.end()) {
            static_cast<void>(world->destroy(kMirror->second));
            if (kMirror->second == owned) {
                owned = {};
                ownedNet = 0;
                if (prediction) {
                    prediction->reset();
                }
            }
            mirrored.erase(kMirror);
            if (interpolation) {
                interpolation->retire(kRecord->entity.value);
            }
            // IDs are never reused in an epoch, so nothing late can reach a
            // replacement; what is kept of the retired one can go.
            appliedAt.erase(appliedAt.lower_bound({kRecord->entity.value, 0}),
                            appliedAt.lower_bound({kRecord->entity.value + 1, 0}));
        }
        acknowledge(network::ControlFrame::MappingRetireAck, kRecord->entity);
    }

    /// The server says how early this client's input arrives; a client that
    /// is late labels its next commands further ahead at once, and one far
    /// early, as a client is that stalled, was paced ahead, and made up what
    /// it owed, leaves samples unlabelled, one in five, until it is not
    /// (D317). A tick once labelled keeps its command.
    void onPace(const network::SessionEvent& event) {
        const auto kPace = decodePace(event.payload);
        if (!kPace.has_value()) {
            ++statistics.datagramsRefused;
            return;
        }
        if (event.sequence <= paceSequence) {
            return;
        }
        paceSequence = event.sequence;
        // The pace after a jump measured the ticks before it took effect.
        if (std::exchange(jumped, false)) {
            return;
        }
        const auto kTarget = static_cast<std::int64_t>(kPace->targetLead);
        if (kPace->measuredLead < kTarget) {
            nextInputTick += static_cast<std::uint64_t>(kTarget - kPace->measuredLead);
            holdBack = 0;
            jumped = true;
        } else if (kPace->measuredLead > kTarget + kLeadSlack) {
            // Set, not added: the paces in flight measure the same lead.
            holdBack = static_cast<std::uint64_t>(kPace->measuredLead - kTarget);
        } else {
            holdBack = 0;
        }
    }

    /// Notes a state datagram as received: the newest sequence, and a bit per
    /// earlier one within the acknowledgement's reach.
    void receive(std::uint64_t sequence) noexcept {
        constexpr std::uint64_t kBits = 64;
        if (sequence > received.latest) {
            const std::uint64_t kShift = sequence - received.latest;
            const std::uint64_t kKept = kShift >= kBits ? 0 : received.earlier << kShift;
            const std::uint64_t kOld = received.latest != 0 && kShift <= kBits ? std::uint64_t{1} << (kShift - 1) : 0;
            received.earlier = kKept | kOld;
            received.latest = sequence;
        } else if (sequence < received.latest && received.latest - sequence <= kBits) {
            received.earlier |= std::uint64_t{1} << (received.latest - sequence - 1);
        }
        acknowledgementDue = true;
    }

    void sendAcknowledgement() {
        acknowledgementDue = false;
        scratch.resize(32);
        network::Writer writer{scratch};
        if (encodeStateAck(writer, received).has_value() &&
            sessions
                ->sendDatagram(*connection,
                               network::DatagramRecord{.lane = network::DatagramLane::Input,
                                                       .laneEpoch = accept->inputEpoch,
                                                       .sequence = ++inputSequence,
                                                       .payloadType = kStateAckPayload,
                                                       .payload = writer.written()})
                .has_value()) {
            ++statistics.acknowledgementsSent;
        }
    }

    void onState(const network::SessionEvent& event) {
        if (event.laneEpoch == accept->replicationEpoch && event.payloadType == kPacePayload) {
            onPace(event);
            return;
        }
        if (event.laneEpoch != accept->replicationEpoch || event.payloadType != kStatePayload) {
            ++statistics.datagramsRefused;
            return;
        }
        // Decode it all first; apply only a datagram that decodes whole.
        network::Reader reader{event.payload};
        const auto kHeader = decodeStateHeader(reader);
        if (!kHeader.has_value()) {
            ++statistics.datagramsRefused;
            return;
        }
        staging.clear();
        staged.clear();
        std::uint64_t unmapped = 0;
        for (std::uint64_t index = 0; index < kHeader->recordCount; ++index) {
            const auto kHead = decodeStateRecordHead(reader, settings.table.components.size());
            if (!kHead.has_value()) {
                ++statistics.datagramsRefused;
                return;
            }
            const ComponentCodec& codec = settings.table.components[kHead->component];
            const std::size_t kOffset = staging.size();
            staging.resize(kOffset + codec.size);
            if (!codec.decode(reader, staging.data() + kOffset, &names).has_value()) {
                ++statistics.datagramsRefused;
                return;
            }
            const auto kMirror = mirrored.find(kHead->entity.value);
            if (kMirror == mirrored.end()) {
                ++unmapped;
                continue;
            }
            const auto kApplied = appliedAt.find({kHead->entity.value, kHead->component});
            if (kApplied != appliedAt.end() && kApplied->second > kHeader->serverTick) {
                ++statistics.recordsStale;
                continue;
            }
            staged.push_back(Staged{.net = kHead->entity.value,
                                    .entity = kMirror->second,
                                    // Checked against the table as it was read.
                                    .component = static_cast<std::size_t>(kHead->component),
                                    .offset = kOffset});
        }
        if (reader.remaining() != 0) {
            ++statistics.datagramsRefused;
            return;
        }
        ++statistics.stateDatagrams;
        statistics.recordsUnmapped += unmapped;
        receive(event.sequence);
        if (interpolation) {
            interpolation->heard(kHeader->serverTick);
        }
        for (const Staged& record : staged) {
            appliedAt[{record.net, record.component}] = kHeader->serverTick;
            ++statistics.recordsApplied;
            if (interpolation && interpolation->interpolates(record.component) && record.net != ownedNet) {
                // Shown when the moment shown reaches it.
                interpolation->sample(
                    record.net,
                    record.component,
                    kHeader->serverTick,
                    std::span{staging}.subspan(record.offset, settings.table.components[record.component].size));
                continue;
            }
            const schema::ComponentRuntimeId kId = table[record.component];
            void* const kValue = world->getErased(record.entity, kId);
            if (kValue != nullptr) {
                std::memcpy(kValue, staging.data() + record.offset, settings.table.components[record.component].size);
            } else {
                static_cast<void>(world->insertErased(record.entity, kId, staging.data() + record.offset));
            }
        }
        serverTick = std::max(serverTick, kHeader->serverTick);
        stateSequence = std::max(stateSequence, event.sequence);
        consumedInputTick = std::max(consumedInputTick, kHeader->consumedInputTick);
        if (prediction) {
            reconcile(kHeader->consumedInputTick, kHeader->serverTick, kHeader->playerWhole);
        }
    }

    /// Hands the server's values for the player's predicted components, as
    /// staged from this datagram, to the prediction, and shows its result.
    /// With `whole`, the server says the player's values not here are the
    /// ones this client holds, so a state without them still confirms
    /// (D249), and a confirmation is of the whole state, the only kind a
    /// checksum is sent for (D274).
    void reconcile(std::uint64_t consumed, std::uint64_t tick, bool whole) {
        std::vector<std::span<const std::byte>> values(prediction->count());
        bool any = false;
        for (const Staged& record : staged) {
            const auto& kIndex = predictedIndex[record.component];
            if (record.net == ownedNet && ownedNet != 0 && kIndex.has_value()) {
                values[*kIndex] =
                    std::span{staging}.subspan(record.offset, settings.table.components[record.component].size);
                any = true;
            }
        }
        if (any || whole) {
            const std::uint32_t kInterval = settings.prediction->checksumInterval;
            const bool kConfirmed = prediction->authoritative(consumed, values, whole);
            if (kConfirmed && whole && kInterval != 0 && ++confirmations % kInterval == 0) {
                sendChecksum(tick);
            }
            present();
        }
    }

    /// SPEC-0041's checksum record of the whole predicted scope at the
    /// confirmed server tick `tick`, each value in its codec's wire form
    /// (D204).
    void sendChecksum(std::uint64_t tick) {
        const auto& kConfirmed = prediction->confirmed();
        std::vector<std::byte> wire;
        std::vector<std::size_t> ends;
        for (std::size_t index = 0; index < kConfirmed.size() && index < predictedTable.size(); ++index) {
            const ComponentCodec& codec = settings.table.components[predictedTable[index]];
            const std::size_t kAt = wire.size();
            wire.resize(kAt + codec.wireSize());
            network::Writer writer{std::span{wire}.subspan(kAt)};
            if (kConfirmed[index].size() != codec.size || !codec.encode(kConfirmed[index].data(), writer).has_value()) {
                return;
            }
            ends.push_back(wire.size());
        }
        if (settings.prediction->divergenceDrill && !wire.empty()) {
            wire[0] ^= std::byte{1};
        }
        std::vector<std::span<const std::byte>> values;
        for (std::size_t index = 0; index < ends.size(); ++index) {
            const std::size_t kFrom = index == 0 ? 0 : ends[index - 1];
            values.push_back(std::span{wire}.subspan(kFrom, ends[index] - kFrom));
        }
        const std::uint64_t kChecksum = predictedChecksum(values);
        if (settings.prediction->captureChecksums) {
            ChecksumCapture& kept = captures.emplace_back(ChecksumCapture{.tick = tick, .checksum = kChecksum});
            for (const std::span<const std::byte> kValue : values) {
                kept.values.emplace_back(kValue.begin(), kValue.end());
            }
            if (captures.size() > kChecksumsCaptured) {
                captures.pop_front();
            }
        }
        scratch.resize(32);
        network::Writer writer{scratch};
        if (encodeChecksum(writer, ChecksumRecord{.tick = tick, .scope = scope, .checksum = kChecksum}).has_value() &&
            sessions
                ->sendDatagram(*connection,
                               network::DatagramRecord{.lane = network::DatagramLane::Input,
                                                       .laneEpoch = accept->inputEpoch,
                                                       .sequence = ++inputSequence,
                                                       .payloadType = kChecksumPayload,
                                                       .payload = writer.written()})
                .has_value()) {
            ++checksumsSent;
        }
    }

    /// Every other mirrored entity's neighborhood values as last heard, for
    /// the predictor: the newest state for an interpolated component, not
    /// the one shown.
    void placeNeighbors() {
        neighborValues.clear();
        for (const auto& [net, entity] : mirrored) {
            if (net == ownedNet) {
                continue;
            }
            for (const std::size_t kIndex : neighborhood) {
                const ComponentCodec& codec = settings.table.components[kIndex];
                std::span<const std::byte> value;
                if (interpolation && interpolation->interpolates(kIndex)) {
                    value = interpolation->newest(net, kIndex);
                } else if (const void* const kHeld = std::as_const(*world).getErased(entity, table[kIndex])) {
                    value = std::span{static_cast<const std::byte*>(kHeld), codec.size};
                }
                if (!value.empty()) {
                    neighborValues.push_back(
                        NeighborValue{.entity = net, .component = codec.component, .value = value});
                }
            }
        }
        static_cast<void>(settings.prediction->predictor->place(neighborValues));
    }

    /// The player shows what is predicted for it, not the older server state.
    void present() {
        if (owned.isNull()) {
            return;
        }
        for (std::size_t index = 0; index < predictedIndex.size(); ++index) {
            if (!predictedIndex[index].has_value()) {
                continue;
            }
            const auto kValue = prediction->current(*predictedIndex[index]);
            if (!kValue.has_value() || kValue->size() != settings.table.components[index].size) {
                continue;
            }
            void* const kInto = world->getErased(owned, table[index]);
            if (kInto != nullptr) {
                std::memcpy(kInto, kValue->data(), kValue->size());
            }
        }
    }
};

ReplicationClient::ReplicationClient(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ReplicationClient::~ReplicationClient() = default;

result::Result<std::unique_ptr<ReplicationClient>>
ReplicationClient::create(network::Sessions& sessions, world::World& world, ClientReplicationSettings settings) {
    auto state = std::make_unique<State>();
    for (const ComponentCodec& codec : settings.table.components) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, world.registry().find(codec.component));
        const schema::ComponentDescriptor& descriptor = world.registry().descriptor(kId);
        if (!codec.valid() || !descriptor.plainData || descriptor.size != codec.size) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "a replicated component is not plain data of its codec's size");
        }
        state->table.push_back(kId);
    }
    if (settings.input && !settings.input->valid()) {
        return refuse(
            result::ErrorClass::InvalidArgument, ReplicationError::FieldUnsupported, "an invalid input codec");
    }
    state->predictedIndex.resize(settings.table.components.size());
    if (settings.prediction) {
        const PredictionSettings& kPrediction = *settings.prediction;
        if (kPrediction.predictor == nullptr || !settings.input || kPrediction.predicted.empty() ||
            kPrediction.window == 0) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "prediction needs a predictor, input, predicted components, and a window");
        }
        for (std::size_t index = 0; index < kPrediction.predicted.size(); ++index) {
            const auto kIn = std::find_if(
                settings.table.components.begin(), settings.table.components.end(), [&](const ComponentCodec& codec) {
                    return codec.component == kPrediction.predicted[index];
                });
            if (kIn == settings.table.components.end()) {
                return refuse(result::ErrorClass::InvalidArgument,
                              ReplicationError::FieldUnsupported,
                              "a predicted component does not replicate");
            }
            state->predictedIndex[static_cast<std::size_t>(kIn - settings.table.components.begin())] = index;
            state->predictedTable.push_back(static_cast<std::size_t>(kIn - settings.table.components.begin()));
        }
        state->scope = scopeFingerprint(settings.prediction->predicted);
        state->prediction.emplace(kPrediction, settings.input->size);
    }
    if (settings.interpolation) {
        const InterpolationSettings& kInterpolation = *settings.interpolation;
        std::vector<bool> interpolated(settings.table.components.size());
        for (const schema::ComponentTypeId kComponent : kInterpolation.interpolated) {
            const auto kIn = std::ranges::find(settings.table.components, kComponent, &ComponentCodec::component);
            if (kIn == settings.table.components.end()) {
                return refuse(result::ErrorClass::InvalidArgument,
                              ReplicationError::FieldUnsupported,
                              "an interpolated component does not replicate");
            }
            interpolated[static_cast<std::size_t>(kIn - settings.table.components.begin())] = true;
        }
        if (kInterpolation.clock == nullptr || kInterpolation.maximumSpan == 0 || kInterpolation.samples < 2) {
            return refuse(result::ErrorClass::InvalidArgument,
                          ReplicationError::FieldUnsupported,
                          "interpolation needs a clock, a span, and room for two states");
        }
        state->interpolation.emplace(kInterpolation, settings.table.components, std::move(interpolated));
    }
    if (settings.prediction) {
        for (const schema::ComponentTypeId kComponent : settings.prediction->neighborhood) {
            const auto kIn = std::ranges::find(settings.table.components, kComponent, &ComponentCodec::component);
            if (kIn == settings.table.components.end()) {
                return refuse(result::ErrorClass::InvalidArgument,
                              ReplicationError::FieldUnsupported,
                              "a neighborhood component does not replicate");
            }
            state->neighborhood.push_back(static_cast<std::size_t>(kIn - settings.table.components.begin()));
        }
    }
    state->sessions = &sessions;
    state->world = &world;
    state->settings = std::move(settings);
    if (state->prediction && !state->neighborhood.empty()) {
        state->prediction->neighbors([raw = state.get()] {
            raw->placeNeighbors();
        });
    }
    return std::make_unique<ReplicationClient>(std::move(state));
}

result::Status ReplicationClient::connect(const network::Endpoint& endpoint, const network::Hello& hello) {
    RAWFRAME_TRY_ASSIGN(const network::ConnectionId kConnection, state_->sessions->connect(endpoint, hello));
    state_->connection = kConnection;
    return {};
}

void ReplicationClient::pump() {
    State& state = *state_;
    state.events.clear();
    state.sessions->pump(state.events);
    for (const network::SessionEvent& event : state.events) {
        if (!state.connection || !(event.connection == *state.connection)) {
            continue;
        }
        switch (event.kind) {
        case network::SessionEventKind::Admitted:
            state.accept = event.accept;
            state.consumedInputTick = event.accept.tickOrigin;
            state.nextInputTick = event.accept.tickOrigin + 1;
            if (state.interpolation) {
                state.interpolation->admitted(event.accept.tickRateTicks, event.accept.tickRateSeconds);
            }
            if (state.prediction) {
                if (const auto kRate = world::TickRate::of(static_cast<std::uint32_t>(event.accept.tickRateTicks),
                                                           static_cast<std::uint32_t>(event.accept.tickRateSeconds))) {
                    state.settings.prediction->predictor->rate(*kRate);
                }
                state.prediction->ticksPerSecond((event.accept.tickRateTicks + event.accept.tickRateSeconds - 1) /
                                                 std::max<std::uint64_t>(event.accept.tickRateSeconds, 1));
            }
            break;
        case network::SessionEventKind::Frame:
            if (state.accept) {
                state.onFrame(event);
            }
            break;
        case network::SessionEventKind::Datagram:
            if (state.accept) {
                state.onState(event);
            }
            break;
        case network::SessionEventKind::Event:
            // The engine's lane says the session is over: this side closes
            // and forgets the connection, as when it ends.
            if (state.accept && event.eventLane == network::kEngineLane &&
                event.payloadType == network::kSessionTerminated) {
                if (auto termination = network::decodeTermination(event.payload)) {
                    state.termination = std::move(*termination);
                }
                state.sessions->close(event.connection);
                state.end();
                break;
            }
            // The game's lane is the one a client declares, and only when
            // it has somewhere to put what arrives.
            if (state.accept && event.eventLane == kGameMessageLane && state.settings.messages != nullptr &&
                event.payloadType <= std::numeric_limits<std::uint32_t>::max()) {
                ++state.statistics.messagesReceived;
                state.settings.messages->deliver(
                    ReceivedMessage{.kind = static_cast<std::uint32_t>(event.payloadType), .value = event.payload});
            }
            break;
        case network::SessionEventKind::Rejected:
            state.rejection = event.reject.reason;
            break;
        case network::SessionEventKind::Ended:
            state.end();
            break;
        }
    }
    // One acknowledgement per pump covers every state datagram since the last.
    if (state.accept && state.acknowledgementDue) {
        state.sendAcknowledgement();
    }
    if (state.accept && state.interpolation) {
        state.interpolation->show(*state.world, state.mirrored, state.table);
    }
}

bool ReplicationClient::admitted() const noexcept {
    return state_->accept.has_value();
}

std::optional<network::RejectReason> ReplicationClient::rejection() const noexcept {
    return state_->rejection;
}

bool ReplicationClient::serverStopping() const noexcept {
    return state_->serverStopping;
}

const std::optional<network::Termination>& ReplicationClient::termination() const noexcept {
    return state_->termination;
}

const std::optional<network::Accept>& ReplicationClient::accept() const noexcept {
    return state_->accept;
}

bool ReplicationClient::ended() const noexcept {
    return state_->ended;
}

world::EntityHandle ReplicationClient::owned() const noexcept {
    return state_->owned;
}

std::uint64_t ReplicationClient::serverTick() const noexcept {
    return state_->serverTick;
}

result::Status ReplicationClient::submitInput(std::span<const std::byte> value) {
    State& state = *state_;
    if (!state.accept || !state.settings.input || value.size() != state.settings.input->size) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      ReplicationError::InputRefused,
                      "input needs an admitted session and a value of the input component's size");
    }
    // Commands the server consumed are done.
    state.unconsumed.erase(state.unconsumed.begin(), state.unconsumed.upper_bound(state.consumedInputTick));
    // Far early: this sample is left unlabelled and unpredicted, so the
    // input's lead shrinks by a tick; the window goes out as ever (D317).
    const bool kHeldBack = state.holdBack > 0 && state.sinceHeld + 1 >= kDilationSamples;
    if (kHeldBack) {
        --state.holdBack;
        state.sinceHeld = 0;
        ++state.statistics.samplesHeldBack;
        if (state.unconsumed.empty()) {
            return {};
        }
    } else {
        ++state.sinceHeld;
    }
    std::vector<std::byte> wire(state.settings.input->wireSize() +
                                (state.settings.perception ? kMaximumPerceptionBytes : 0));
    network::Writer commandWriter{wire};
    RAWFRAME_TRY(state.settings.input->encode(value.data(), commandWriter, &state.names));
    if (state.settings.perception) {
        // What is shown now: between two states, or the newest.
        PerceptionContext seen{.baseTick = state.serverTick, .fraction = 0};
        if (const auto kShown = state.interpolation ? state.interpolation->perceivedTick() : std::nullopt;
            kShown.has_value() && *kShown > 0) {
            seen.baseTick = static_cast<std::uint64_t>(*kShown);
            seen.fraction = static_cast<std::uint16_t>((*kShown - static_cast<double>(seen.baseTick)) * 65536.0);
        }
        RAWFRAME_TRY(encodePerception(commandWriter, seen));
    }
    wire.resize(commandWriter.written().size());
    if (!kHeldBack) {
        const std::uint64_t kTick = state.nextInputTick++;
        state.unconsumed[kTick] = std::move(wire);
        if (state.prediction) {
            state.prediction->command(kTick, value);
            state.present();
        }
    }
    while (state.unconsumed.size() > kMaximumInputWindow) {
        state.unconsumed.erase(state.unconsumed.begin());
    }
    InputWindow window{.newestInputTick = state.unconsumed.rbegin()->first,
                       .ackedStateSequence = state.stateSequence,
                       .ackedServerTick = state.serverTick,
                       .commands = {}};
    for (const auto& [tick, command] : state.unconsumed) {
        window.commands.emplace_back(command);
    }
    state.scratch.resize(2048);
    network::Writer writer{state.scratch};
    RAWFRAME_TRY(encodeInputWindow(writer, window));
    RAWFRAME_TRY(state.sessions->sendDatagram(*state.connection,
                                              network::DatagramRecord{.lane = network::DatagramLane::Input,
                                                                      .laneEpoch = state.accept->inputEpoch,
                                                                      .sequence = ++state.inputSequence,
                                                                      .payloadType = kInputWindowPayload,
                                                                      .payload = writer.written()}));
    ++state.statistics.inputWindowsSent;
    return {};
}

ClientReplicationStatistics ReplicationClient::statistics() const noexcept {
    return state_->statistics;
}

PredictionStatistics ReplicationClient::predictionStatistics() const noexcept {
    if (!state_->prediction) {
        return {};
    }
    PredictionStatistics statistics = state_->prediction->statistics();
    statistics.checksumsSent = state_->checksumsSent;
    return statistics;
}

std::vector<ChecksumCapture> ReplicationClient::checksumCaptures() const {
    return {state_->captures.begin(), state_->captures.end()};
}

std::optional<double> ReplicationClient::perceivedTick() const noexcept {
    return state_->interpolation ? state_->interpolation->perceivedTick() : std::nullopt;
}

InterpolationStatistics ReplicationClient::interpolationStatistics() const noexcept {
    return state_->interpolation ? state_->interpolation->statistics() : InterpolationStatistics{};
}

} // namespace rawframe::world_replication
