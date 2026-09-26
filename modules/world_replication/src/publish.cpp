// Publishing: committed state out to every connection each tick, within its
// interest and its byte budget, the connection's own player first (D26, D28,
// D31, D48), with the server's checksum of its predicted scope kept (D204).

#include "rawframe/world_replication/checksum.h"
#include "server_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>

namespace rawframe::world_replication {

void ReplicationServer::State::publish(world::World& world, world::TickIndex tick) {
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

void ReplicationServer::State::locate(world::World& world) {
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

const std::array<double, 3>* ReplicationServer::State::locationOf(world::EntityHandle entity) const noexcept {
    const auto kFound =
        std::lower_bound(located.begin(), located.end(), entity, [](const Located& value, world::EntityHandle key) {
            return value.entity < key;
        });
    return kFound != located.end() && kFound->entity == entity ? &kFound->at : nullptr;
}

bool ReplicationServer::State::relevant(const Peer& peer,
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

bool ReplicationServer::State::isPresent(world::EntityHandle entity) const noexcept {
    const auto kFound = std::lower_bound(
        entities.begin(), entities.end(), entity, [](const PresentEntity& value, world::EntityHandle key) {
            return value.entity < key;
        });
    return kFound != entities.end() && kFound->entity == entity;
}

void ReplicationServer::State::sendPace(Peer& peer) {
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

void ReplicationServer::State::keepChecksum(Peer& peer, world::TickIndex tick) {
    if (predicted.empty()) {
        return;
    }
    scopeValues.assign(predicted.size(), {});
    const auto kFirst = std::ranges::lower_bound(present, peer.player, {}, &PresentValue::entity);
    for (auto at = kFirst; at != present.end() && at->entity == peer.player; ++at) {
        const auto kPlace = std::ranges::find(predicted, at->component);
        if (kPlace != predicted.end()) {
            scopeValues[static_cast<std::size_t>(kPlace - predicted.begin())] =
                std::span{encoded}.subspan(at->offset, at->length);
        }
    }
    peer.checksums.keep(tick.value, predictedChecksum(scopeValues));
}

void ReplicationServer::State::publishTo(Peer& peer, world::TickIndex tick) {
    keepChecksum(peer, tick);
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
                peer.sent.push_back(
                    SentState{.sequence = peer.stateSequence, .tick = tick.value, .records = std::move(inDatagram)});
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
        return naming[value.component] != 0 ? std::span<const std::byte>{named}.subspan(namedAt[index], value.length)
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
        if (replica.sent && tick.value - replica.sentAt < settings.resendAfter && sameBytes(replica.lastSent, kValue)) {
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

} // namespace rawframe::world_replication
