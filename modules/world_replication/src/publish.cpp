// Publishing: committed state out to every connection each tick, within its
// interest and its byte budget, the connection's own player first (D26, D28,
// D31, D48), with the server's checksum of its predicted scope kept (D204).
// Each connection looks only at the interest cells around its player and at
// what it already holds, so its work follows its interest, not the World
// (D209).

#include "rawframe/world_replication/checksum.h"
#include "server_state.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>

namespace rawframe::world_replication {

namespace {

/// Cells are a hundredth wider than the entering radius: two coordinates
/// within the radius of each other then land in the same cell or the next
/// despite rounding.
constexpr double kCellSlack = 1.01;

/// The cell a finite coordinate is in, for cells `width` wide. Far out,
/// cells are clamped, which keeps coordinates in the same cell or the next
/// where they were.
std::int64_t cellOf(double coordinate, double width) noexcept {
    constexpr double kFar = 1099511627776.0;
    return static_cast<std::int64_t>(std::clamp(std::floor(coordinate / width), -kFar, kFar));
}

/// A cell's bucket among `mask` + 1: any mix does, since a bucket's
/// entities are only candidates, each checked by distance.
std::size_t bucketFor(const std::array<std::int64_t, 3>& cell, std::size_t mask) noexcept {
    std::uint64_t mixed = (static_cast<std::uint64_t>(cell[0]) * 0x9e3779b97f4a7c15U) ^
                          (static_cast<std::uint64_t>(cell[1]) * 0xc2b2ae3d27d4eb4fU) ^
                          (static_cast<std::uint64_t>(cell[2]) * 0x165667b19e3779f9U);
    mixed ^= mixed >> 29U;
    return static_cast<std::size_t>(mixed) & mask;
}

bool finite(const std::array<double, 3>& at) noexcept {
    return std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
}

} // namespace

void ReplicationServer::State::publish(world::World& world, world::TickIndex tick) {
    gathered.clear();
    encoded.clear();
    std::uint32_t slots = 0;
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
                slots = std::max(slots, chunk.entities[row].slot + 1);
                gathered.push_back(PresentValue{.entity = chunk.entities[row],
                                                .component = index,
                                                .offset = kOffset,
                                                .length = kWire,
                                                .source = chunk.columns[0] + (row * codec.size)});
            }
        });
    }
    // Into entity then component order without comparing: a World has one
    // live entity in a slot, so slot order is entity order, and a counting
    // sort by slot keeps the component order the values were gathered in.
    valuesAt.assign(std::size_t{slots} + 1, 0);
    for (const PresentValue& value : gathered) {
        ++valuesAt[value.entity.slot + 1];
    }
    for (std::size_t slot = 1; slot < valuesAt.size(); ++slot) {
        valuesAt[slot] += valuesAt[slot - 1];
    }
    present.resize(gathered.size());
    filling.assign(valuesAt.begin(), valuesAt.end() - 1);
    for (const PresentValue& value : gathered) {
        present[filling[value.entity.slot]++] = value;
    }
    entities.clear();
    entityAt.assign(slots, kNowhere);
    for (std::uint32_t slot = 0; slot < slots; ++slot) {
        if (valuesAt[slot] != valuesAt[slot + 1]) {
            entityAt[slot] = entities.size();
            entities.push_back(PresentEntity{.entity = present[valuesAt[slot]].entity, .located = false, .at = {}});
        }
    }
    locate(world);
    if (settings.interest) {
        cellEntities();
    }
    heldMark.assign(entities.size(), 0);
    markNow = 0;
    for (auto& [id, peer] : peers) {
        publishTo(peer, tick);
    }
}

void ReplicationServer::State::locate(world::World& world) {
    located.clear();
    locatedAt.clear();
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
            if (entry.entity.slot >= locatedAt.size()) {
                locatedAt.resize(std::size_t{entry.entity.slot} + 1, kNowhere);
            }
            locatedAt[entry.entity.slot] = located.size();
            located.push_back(entry);
            if (const std::size_t kIndex = presentIndex(entry.entity); kIndex != kNowhere) {
                entities[kIndex].located = true;
                entities[kIndex].at = entry.at;
            }
        }
    });
}

void ReplicationServer::State::cellEntities() {
    unplaced.clear();
    const std::size_t kBuckets = std::bit_ceil(std::max<std::size_t>(entities.size() * 2, 16));
    bucketMask = kBuckets - 1;
    bucketAt.assign(kBuckets + 1, 0);
    bucketOf.assign(entities.size(), kNowhere);
    const double kWidth = settings.interest->radius * kCellSlack;
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const PresentEntity& entity = entities[index];
        if (!entity.located || !finite(entity.at)) {
            unplaced.push_back(index);
            continue;
        }
        bucketOf[index] = bucketFor(
            {cellOf(entity.at[0], kWidth), cellOf(entity.at[1], kWidth), cellOf(entity.at[2], kWidth)}, bucketMask);
        ++bucketAt[bucketOf[index] + 1];
    }
    for (std::size_t bucket = 1; bucket <= kBuckets; ++bucket) {
        bucketAt[bucket] += bucketAt[bucket - 1];
    }
    bucketed.resize(bucketAt[kBuckets]);
    filling.assign(bucketAt.begin(), bucketAt.end() - 1);
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (bucketOf[index] != kNowhere) {
            bucketed[filling[bucketOf[index]]++] = index;
        }
    }
}

void ReplicationServer::State::reach(const std::array<double, 3>& viewer) {
    reachable.assign(unplaced.begin(), unplaced.end());
    if (!finite(viewer)) {
        // Rare enough to look at everything, and never in reach of what is.
        reachable.resize(entities.size());
        for (std::size_t index = 0; index < entities.size(); ++index) {
            reachable[index] = index;
        }
        return;
    }
    const double kWidth = settings.interest->radius * kCellSlack;
    const std::array<std::int64_t, 3> kAt = {
        cellOf(viewer[0], kWidth), cellOf(viewer[1], kWidth), cellOf(viewer[2], kWidth)};
    // One to three axes: the cells around the viewer's along each, and its
    // own along the axes interest does not use, where every entity is at
    // nought. Each bucket once, however many of those cells it holds.
    const std::size_t kAxes = settings.interest->axes.size();
    const std::int64_t kSpan[3] = {1, kAxes > 1 ? 1 : 0, kAxes > 2 ? 1 : 0};
    std::array<std::size_t, 27> buckets{};
    std::size_t count = 0;
    for (std::int64_t x = -kSpan[0]; x <= kSpan[0]; ++x) {
        for (std::int64_t y = -kSpan[1]; y <= kSpan[1]; ++y) {
            for (std::int64_t z = -kSpan[2]; z <= kSpan[2]; ++z) {
                buckets[count++] = bucketFor({kAt[0] + x, kAt[1] + y, kAt[2] + z}, bucketMask);
            }
        }
    }
    std::sort(buckets.begin(), buckets.begin() + static_cast<std::ptrdiff_t>(count));
    for (std::size_t at = 0; at < count; ++at) {
        if (at == 0 || buckets[at] != buckets[at - 1]) {
            reachable.insert(reachable.end(),
                             bucketed.begin() + static_cast<std::ptrdiff_t>(bucketAt[buckets[at]]),
                             bucketed.begin() + static_cast<std::ptrdiff_t>(bucketAt[buckets[at] + 1]));
        }
    }
}

const std::array<double, 3>* ReplicationServer::State::locationOf(world::EntityHandle entity) const noexcept {
    if (entity.slot >= locatedAt.size() || locatedAt[entity.slot] == kNowhere) {
        return nullptr;
    }
    const Located& place = located[locatedAt[entity.slot]];
    return place.entity == entity ? &place.at : nullptr;
}

std::size_t ReplicationServer::State::presentIndex(world::EntityHandle entity) const noexcept {
    if (entity.slot >= entityAt.size() || entityAt[entity.slot] == kNowhere) {
        return kNowhere;
    }
    const std::size_t kIndex = entityAt[entity.slot];
    return entities[kIndex].entity == entity ? kIndex : kNowhere;
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
    return presentIndex(entity) != kNowhere;
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
    if (isPresent(peer.player)) {
        for (std::size_t at = valuesAt[peer.player.slot]; at != valuesAt[peer.player.slot + 1]; ++at) {
            const auto kPlace = std::ranges::find(predicted, present[at].component);
            if (kPlace != predicted.end()) {
                scopeValues[static_cast<std::size_t>(kPlace - predicted.begin())] =
                    std::span{encoded}.subspan(present[at].offset, present[at].length);
            }
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
    // in this epoch. What stays is marked as held for this connection.
    ++markNow;
    peer.mapped.keepIf([&](Mappings::Entry& mapping) {
        const std::size_t kIndex = presentIndex(mapping.first);
        const bool kPresent = kIndex != kNowhere;
        if (kPresent && relevant(peer, kViewer, entities[kIndex], true)) {
            heldMark[kIndex] = markNow;
            return true;
        }
        statistics.interestLeft += kPresent ? 1 : 0;
        if (mapping.second.firstSent.has_value()) {
            peer.retired.push_back(
                Sent{.entity = mapping.first, .from = *mapping.second.firstSent, .until = tick.value});
        }
        sendMapping(peer, network::ControlFrame::MappingRetire, mapping.second.id, false);
        peer.byNetEntity.erase(mapping.second.id.value);
        return false;
    });
    // Declare what is new, before any state names it: the connection's
    // own player first, whatever the bound on mappings.
    const std::size_t kSettled = peer.mapped.size();
    const auto kDeclare = [&](world::EntityHandle entity) {
        const NetEntityId kId{peer.nextNetEntity++};
        peer.mapped.add(entity, Mapping{.id = kId});
        peer.byNetEntity[kId.value] = entity;
        sendMapping(peer, network::ControlFrame::MappingDeclare, kId, entity == peer.player);
    };
    if (!peer.mapped.contains(peer.player) && isPresent(peer.player) && peer.nextNetEntity != 0) {
        kDeclare(peer.player);
        heldMark[presentIndex(peer.player)] = markNow;
    }
    // What enters, declared in entity order up to the bound: few a tick,
    // so only they are put in order.
    entering.clear();
    const auto kConsider = [&](std::size_t index) {
        if (heldMark[index] != markNow && relevant(peer, kViewer, entities[index], false)) {
            entering.push_back(index);
        }
    };
    if (!settings.interest) {
        for (std::size_t index = 0; index < entities.size(); ++index) {
            kConsider(index);
        }
    } else if (kViewer != nullptr) {
        reach(*kViewer);
        for (const std::size_t kIndex : reachable) {
            kConsider(kIndex);
        }
    } else {
        for (const std::size_t kIndex : unplaced) {
            kConsider(kIndex);
        }
    }
    std::ranges::sort(entering);
    for (const std::size_t kIndex : entering) {
        if (peer.mapped.size() >= settings.maximumMapped || peer.nextNetEntity == 0) {
            break;
        }
        kDeclare(entities[kIndex].entity);
    }
    peer.mapped.settle(kSettled);
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
    // Only the values of what the connection holds are looked at: its
    // mappings, in entity order, each found among the present values.
    candidates.clear();
    named.clear();
    const PeerNames kNames{peer};
    const auto kValueOf = [this](std::size_t index, std::size_t namedAt) {
        const PresentValue& value = present[index];
        return naming[value.component] != 0 ? std::span<const std::byte>{named}.subspan(namedAt, value.length)
                                            : std::span<const std::byte>{encoded}.subspan(value.offset, value.length);
    };
    for (auto& [entity, held] : peer.mapped) {
        if (!held.acknowledged || !isPresent(entity)) {
            continue;
        }
        held.replicas.resize(settings.table.components.size());
        for (std::size_t kIndex = valuesAt[entity.slot]; kIndex != valuesAt[entity.slot + 1]; ++kIndex) {
            const PresentValue& value = present[kIndex];
            Replica& replica = held.replicas[value.component];
            // An entity it names goes by this connection's name for it.
            const ComponentCodec& codec = settings.table.components[value.component];
            const std::size_t kNamedAt = named.size();
            if (naming[value.component] != 0) {
                named.resize(named.size() + value.length);
                network::Writer writer{std::span{named}.subspan(kNamedAt)};
                static_cast<void>(codec.encode(value.source, writer, &kNames));
            }
            const std::span<const std::byte> kValue = kValueOf(kIndex, kNamedAt);
            if (replica.held(kValue)) {
                ++statistics.recordsHeld;
                continue;
            }
            if (replica.sent && tick.value - replica.sentAt < settings.resendAfter &&
                sameBytes(replica.lastSent.bytes(), kValue)) {
                continue;
            }
            replica.priority += value.entity == peer.player ? kOwnedPriority : 1;
            candidates.push_back(
                Candidate{.priority = replica.priority, .present = kIndex, .named = kNamedAt, .mapping = &held});
        }
    }
    // Longest waiting first; ties in entity order, so a run repeats.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.priority != right.priority ? left.priority > right.priority : left.present < right.present;
    });

    for (const Candidate& candidate : candidates) {
        const PresentValue& value = present[candidate.present];
        Replica& replica = candidate.mapping->replicas[value.component];
        const std::span<const std::byte> kValue = kValueOf(candidate.present, candidate.named);
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
        if (!replica.sent || !sameBytes(replica.lastSent.bytes(), kValue)) {
            replica.lastSent.assign(kValue);
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
