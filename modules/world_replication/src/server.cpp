#include "rawframe/world_replication/server.h"

#include "rawframe/network/close.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/perception.h"

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

/// What one connection was sent of one entity's component. The value the
/// client holds is `lastSent` once it acknowledges any datagram from
/// `changedAt` on: every record from then carries it.
/// Byte for byte, as one memcmp: std::ranges::equal compares std::byte one
/// at a time, and publish compares every value for every connection.
[[nodiscard]] bool sameBytes(std::span<const std::byte> left, std::span<const std::byte> right) noexcept {
    return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
}

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

/// State datagrams remembered per connection for acknowledgement. One not
/// acknowledged by the time it leaves this window was lost, as far as the
/// server is concerned, and its values go again.
constexpr std::size_t kSentWindow = 256;
/// A state acknowledgement covers its newest sequence and the 64 before it.
constexpr std::uint64_t kAckBits = 64;
/// What a connection's own player gains per tick: always more than anything
/// else can have waited.
constexpr std::uint64_t kOwnedPriority = std::uint64_t{1} << 40U;

/// An entity a connection was sent from one tick until another, before its
/// mapping was retired.
struct Sent {
    world::EntityHandle entity;
    std::uint64_t from = 0;
    std::uint64_t until = 0;
};

/// Ticks of retired mappings the victim gate remembers: the most a physics
/// history keeps.
constexpr std::uint64_t kInterestKept = 1024;

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

/// Bytes a state datagram's framing takes around its payload, at most.
constexpr std::size_t kStateHeaderRoom = 3 * 8;

} // namespace

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
            for (const auto& [id, component] : sent->records) {
                const auto kEntity = peer.byNetEntity.find(id);
                if (kEntity == peer.byNetEntity.end()) {
                    continue;
                }
                const auto kMapping = peer.mapped.find(kEntity->second);
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

    /// A consumed command's moment, checked when it arrived, kept within the
    /// skew of the lag its connection's claims have shown.
    [[nodiscard]] Perception
    perceived(Peer& peer, std::span<const std::byte> command, std::size_t wire, std::uint64_t arrived) {
        const auto kSeen = decodePerception(command.subspan(wire));
        if (!kSeen.has_value()) {
            return Perception{.viewer = peer.viewer};
        }
        const KeptPerception kKept = keepPerception(*kSeen, arrived, settings.perceptionSkew, peer.lag);
        statistics.perceptionsClamped += kKept.clamped ? 1 : 0;
        return Perception{.baseTick = kKept.moment.baseTick, .fraction = kKept.moment.fraction, .viewer = peer.viewer};
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
                    present.push_back(PresentValue{.entity = chunk.entities[row],
                                                   .component = index,
                                                   .offset = kOffset,
                                                   .length = kWire,
                                                   .source = chunk.columns[0] + (row * codec.size)});
                }
            });
        }
        std::sort(present.begin(), present.end(), [](const PresentValue& left, const PresentValue& right) {
            return left.entity != right.entity ? left.entity < right.entity : left.component < right.component;
        });
        locate(world);
        entities.clear();
        auto where = located.begin();
        for (std::size_t index = 0; index < present.size(); ++index) {
            const world::EntityHandle kEntity = present[index].entity;
            if (index != 0 && present[index - 1].entity == kEntity) {
                continue;
            }
            while (where != located.end() && where->entity < kEntity) {
                ++where;
            }
            const bool kLocated = where != located.end() && where->entity == kEntity;
            entities.push_back(PresentEntity{
                .entity = kEntity, .located = kLocated, .at = kLocated ? where->at : std::array<double, 3>{}});
        }
        for (auto& [id, peer] : peers) {
            publishTo(peer, tick);
        }
    }

    void locate(world::World& world) {
        located.clear();
        if (!positions) {
            return;
        }
        const InterestSettings& interest = *settings.interest;
        positions->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                const std::byte* const kValue = chunk.columns[0] + (row * positionSize);
                Located entry{.entity = chunk.entities[row]};
                for (std::size_t axis = 0; axis < std::min(interest.axes.size(), entry.at.size()); ++axis) {
                    const WireField& field = interest.axes[axis];
                    if (field.kind == WireKind::F32) {
                        float value = 0;
                        std::memcpy(&value, kValue + field.offset, sizeof value);
                        entry.at[axis] = value;
                    } else {
                        double value = 0;
                        std::memcpy(&value, kValue + field.offset, sizeof value);
                        entry.at[axis] = value;
                    }
                }
                located.push_back(entry);
            }
        });
        std::sort(located.begin(), located.end(), [](const Located& left, const Located& right) {
            return left.entity < right.entity;
        });
    }

    /// Where a connection's player is, if it has a position.
    [[nodiscard]] const std::array<double, 3>* locationOf(world::EntityHandle entity) const noexcept {
        const auto kFound =
            std::lower_bound(located.begin(), located.end(), entity, [](const Located& value, world::EntityHandle key) {
                return value.entity < key;
            });
        return kFound != located.end() && kFound->entity == entity ? &kFound->at : nullptr;
    }

    /// Whether `entity` is in the interest of a connection whose player is
    /// at `viewer`; `mapped` says whether it already is, and so whether it
    /// is held to the leaving radius or the entering one.
    [[nodiscard]] bool relevant(const Peer& peer,
                                const std::array<double, 3>* viewer,
                                const PresentEntity& entity,
                                bool mapped) const noexcept {
        if (!settings.interest || entity.entity == peer.player || !entity.located) {
            return true;
        }
        if (viewer == nullptr) {
            return false;
        }
        double distance = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double kDelta = entity.at[axis] - (*viewer)[axis];
            distance += kDelta * kDelta;
        }
        const double kLimit = mapped ? settings.interest->leaveRadius : settings.interest->radius;
        // A position that is not a number is never within reach.
        return distance <= kLimit * kLimit;
    }

    [[nodiscard]] bool isPresent(world::EntityHandle entity) const noexcept {
        const auto kFound = std::lower_bound(
            entities.begin(), entities.end(), entity, [](const PresentEntity& value, world::EntityHandle key) {
                return value.entity < key;
            });
        return kFound != entities.end() && kFound->entity == entity;
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
        std::erase_if(peer.retired, [&](const Sent& gone) {
            return gone.until + kInterestKept < tick.value;
        });
        if (settings.input && peer.heardInput && tick.value % settings.paceInterval == 0) {
            sendPace(peer);
        }
        const std::array<double, 3>* const kViewer = locationOf(peer.player);
        // Retire what is gone or out of interest; the ID is never used again
        // in this epoch. Both in entity order: one walk along the entities.
        auto walk = entities.begin();
        for (auto mapping = peer.mapped.begin(); mapping != peer.mapped.end();) {
            while (walk != entities.end() && walk->entity < mapping->first) {
                ++walk;
            }
            const bool kPresent = walk != entities.end() && walk->entity == mapping->first;
            if (kPresent && relevant(peer, kViewer, *walk, true)) {
                ++mapping;
                continue;
            }
            statistics.interestLeft += kPresent ? 1 : 0;
            if (mapping->second.firstSent.has_value()) {
                peer.retired.push_back(
                    Sent{.entity = mapping->first, .from = *mapping->second.firstSent, .until = tick.value});
            }
            sendMapping(peer, network::ControlFrame::MappingRetire, mapping->second.id, false);
            peer.byNetEntity.erase(mapping->second.id.value);
            mapping = peer.mapped.erase(mapping);
        }
        // Declare what is new, before any state names it: the connection's
        // own player first, whatever the bound on mappings.
        const auto kDeclare = [&](world::EntityHandle entity) {
            const NetEntityId kId{peer.nextNetEntity++};
            peer.mapped[entity] = Mapping{.id = kId};
            peer.byNetEntity[kId.value] = entity;
            sendMapping(peer, network::ControlFrame::MappingDeclare, kId, entity == peer.player);
        };
        if (!peer.mapped.contains(peer.player) && isPresent(peer.player) && peer.nextNetEntity != 0) {
            kDeclare(peer.player);
        }
        // Both in entity order: one walk along the mappings, which a
        // declaration never moves behind.
        auto known = peer.mapped.begin();
        for (const PresentEntity& entity : entities) {
            while (known != peer.mapped.end() && known->first < entity.entity) {
                ++known;
            }
            if ((known != peer.mapped.end() && known->first == entity.entity) ||
                peer.mapped.size() >= settings.maximumMapped || peer.nextNetEntity == 0 ||
                !relevant(peer, kViewer, entity, false)) {
                continue;
            }
            kDeclare(entity.entity);
        }
        // State for acknowledged mappings, as many datagrams as the byte
        // budget allows.
        const std::size_t kRoom = static_cast<std::size_t>(peer.accept.maximumDatagram);
        records.resize(kRoom);
        network::Writer recordWriter{records};
        std::uint64_t count = 0;
        std::size_t used = 0;
        std::size_t spent = 0;
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
                spent += writer.written().size();
                if (sessions->sendDatagram(peer.connection, kState).has_value()) {
                    ++statistics.stateDatagrams;
                    statistics.stateBytes += writer.written().size();
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

        // What needs sending: not known to be held, and not sent so recently
        // that its acknowledgement may still be on the way.
        candidates.clear();
        Mapping* mapping = nullptr;
        known = peer.mapped.begin();
        named.clear();
        namedAt.assign(present.size(), 0);
        const PeerNames kNames{peer};
        const auto kValueOf = [this](std::size_t index) {
            const PresentValue& value = present[index];
            return naming[value.component] != 0
                       ? std::span<const std::byte>{named}.subspan(namedAt[index], value.length)
                       : std::span<const std::byte>{encoded}.subspan(value.offset, value.length);
        };
        for (std::size_t index = 0; index < present.size(); ++index) {
            const PresentValue& value = present[index];
            if (index == 0 || present[index - 1].entity != value.entity) {
                while (known != peer.mapped.end() && known->first < value.entity) {
                    ++known;
                }
                mapping = known == peer.mapped.end() || known->first != value.entity || !known->second.acknowledged
                              ? nullptr
                              : &known->second;
                if (mapping != nullptr) {
                    mapping->replicas.resize(settings.table.components.size());
                }
            }
            if (mapping == nullptr) {
                continue;
            }
            Replica& replica = mapping->replicas[value.component];
            // An entity it names goes by this connection's name for it.
            const ComponentCodec& codec = settings.table.components[value.component];
            if (naming[value.component] != 0) {
                namedAt[index] = named.size();
                named.resize(named.size() + value.length);
                network::Writer writer{std::span{named}.subspan(namedAt[index])};
                static_cast<void>(codec.encode(value.source, writer, &kNames));
            }
            const std::span<const std::byte> kValue = kValueOf(index);
            if (replica.held(kValue)) {
                ++statistics.recordsHeld;
                continue;
            }
            if (replica.sent && tick.value - replica.sentAt < settings.resendAfter &&
                sameBytes(replica.lastSent, kValue)) {
                continue;
            }
            replica.priority += value.entity == peer.player ? kOwnedPriority : 1;
            candidates.push_back(Candidate{.priority = replica.priority, .present = index, .mapping = mapping});
        }
        // Longest waiting first; ties in entity order, so a run repeats.
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.priority != right.priority ? left.priority > right.priority : left.present < right.present;
        });

        for (const Candidate& candidate : candidates) {
            const PresentValue& value = present[candidate.present];
            Replica& replica = candidate.mapping->replicas[value.component];
            const std::span<const std::byte> kValue = kValueOf(candidate.present);
            const std::size_t kRecord = 10 + value.length;
            if (used + kRecord + kStateHeaderRoom > kRoom) {
                kFlush();
            }
            if (spent + used + kRecord + kStateHeaderRoom > settings.stateBytesPerTick) {
                ++statistics.recordsDeferred;
                continue;
            }
            if (!encodeStateRecordHead(recordWriter, {.entity = candidate.mapping->id, .component = value.component})
                     .has_value() ||
                !recordWriter.bytes(kValue).has_value()) {
                // Larger than a datagram on its own: it cannot be sent.
                recordWriter = network::Writer{records};
                count = 0;
                used = 0;
                inDatagram.clear();
                continue;
            }
            if (!replica.sent || !sameBytes(replica.lastSent, kValue)) {
                replica.lastSent.assign(kValue.begin(), kValue.end());
                replica.changedAt = tick.value;
                replica.sent = true;
            }
            replica.sentAt = tick.value;
            replica.priority = 0;
            candidate.mapping->firstSent = candidate.mapping->firstSent.value_or(tick.value);
            inDatagram.emplace_back(candidate.mapping->id.value, value.component);
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
        const auto kMapped = peer.mapped.find(entity);
        if (kMapped != peer.mapped.end() && kMapped->second.firstSent.has_value()) {
            return kMapped->second.firstSent;
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

} // namespace rawframe::world_replication
