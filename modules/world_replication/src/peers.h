#pragma once

// What the replication server keeps of each admitted connection: what it was
// sent of each entity, what it acknowledged, and its inputs waiting for
// their ticks. Private to the server.

#include "checksums.h"
#include "rawframe/network/admission.h"
#include "rawframe/network/provider.h"
#include "rawframe/world/entity.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/records.h"
#include "rawframe/world_runtime/players.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::world_replication {

/// Byte for byte, as one memcmp: std::ranges::equal compares std::byte one
/// at a time, and publish compares every value for every connection.
[[nodiscard]] inline bool sameBytes(std::span<const std::byte> left, std::span<const std::byte> right) noexcept {
    return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size()) == 0);
}

/// A value's wire bytes, held in place when small, as most components are:
/// every connection compares each of its values every tick, and a value on
/// the heap was a cache miss each time (D211).
class HeldBytes {
public:
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return size_ <= kInPlace ? std::span<const std::byte>{inPlace_.data(), size_} : std::span{heap_};
    }
    void assign(std::span<const std::byte> value) {
        size_ = value.size();
        if (size_ <= kInPlace) {
            std::copy(value.begin(), value.end(), inPlace_.begin());
            heap_.clear();
        } else {
            heap_.assign(value.begin(), value.end());
        }
    }

private:
    static constexpr std::size_t kInPlace = 32;
    std::array<std::byte, kInPlace> inPlace_{};
    std::size_t size_ = 0;
    std::vector<std::byte> heap_;
};

/// What one connection was sent of one entity's component. The value the
/// client holds is `lastSent` once it acknowledges any datagram from
/// `changedAt` on: every record from then carries it.
struct Replica {
    HeldBytes lastSent;
    bool sent = false;
    std::uint64_t changedAt = 0;
    std::uint64_t sentAt = 0;
    std::optional<std::uint64_t> acknowledgedAt;
    /// Grows every tick the value needs sending and is not sent.
    std::uint64_t priority = 0;

    [[nodiscard]] bool held(std::span<const std::byte> value) const noexcept {
        return sent && acknowledgedAt.has_value() && *acknowledgedAt >= changedAt && sameBytes(lastSent.bytes(), value);
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

/// A connection's mappings by entity, in entity order. A sorted vector, not
/// a map: every tick walks them all twice, and a map's scattered nodes made
/// that walk most of a connection's cost (D211).
class Mappings {
public:
    using Entry = std::pair<world::EntityHandle, Mapping>;

    [[nodiscard]] std::vector<Entry>::iterator begin() noexcept {
        return entries_.begin();
    }
    [[nodiscard]] std::vector<Entry>::iterator end() noexcept {
        return entries_.end();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }
    [[nodiscard]] Mapping* find(world::EntityHandle entity) noexcept {
        const auto kAt = std::ranges::lower_bound(entries_, entity, {}, &Entry::first);
        return kAt != entries_.end() && kAt->first == entity ? &kAt->second : nullptr;
    }
    [[nodiscard]] const Mapping* find(world::EntityHandle entity) const noexcept {
        const auto kAt = std::ranges::lower_bound(entries_, entity, {}, &Entry::first);
        return kAt != entries_.end() && kAt->first == entity ? &kAt->second : nullptr;
    }
    [[nodiscard]] bool contains(world::EntityHandle entity) const noexcept {
        return find(entity) != nullptr;
    }
    /// Adds a mapping for an entity it does not hold. Mappings added since
    /// the last `settle` are in no order until it.
    void add(world::EntityHandle entity, Mapping mapping) {
        entries_.emplace_back(entity, std::move(mapping));
    }
    /// Puts added mappings in entity order, `settled` being how many there
    /// were, in order, before the first was added.
    void settle(std::size_t settled) {
        std::sort(entries_.begin() + static_cast<std::ptrdiff_t>(settled),
                  entries_.end(),
                  [](const Entry& left, const Entry& right) {
                      return left.first < right.first;
                  });
        std::inplace_merge(entries_.begin(),
                           entries_.begin() + static_cast<std::ptrdiff_t>(settled),
                           entries_.end(),
                           [](const Entry& left, const Entry& right) {
                               return left.first < right.first;
                           });
    }
    /// Keeps, in order, the mappings `keep` is true for; `keep` sees each
    /// once, in entity order.
    template <typename Keep> void keepIf(Keep&& keep) {
        auto into = entries_.begin();
        for (auto at = entries_.begin(); at != entries_.end(); ++at) {
            if (keep(*at)) {
                if (into != at) {
                    *into = std::move(*at);
                }
                ++into;
            }
        }
        entries_.erase(into, entries_.end());
    }

private:
    std::vector<Entry> entries_;
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

/// An entity a connection was sent from one tick until another, before its
/// mapping was retired.
struct Sent {
    world::EntityHandle entity;
    std::uint64_t from = 0;
    std::uint64_t until = 0;
};

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
    Mappings mapped;
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
    ChecksumBook checksums;
};

/// An entity by the ID of the mapping a connection has acknowledged for it:
/// what it may be named by in a value sent there.
class PeerNames final : public EntityNames {
public:
    explicit PeerNames(const Peer& peer) noexcept : peer_(&peer) {
    }
    [[nodiscard]] std::uint32_t netOf(world::EntityHandle entity) const noexcept override {
        const Mapping* const kMapping = peer_->mapped.find(entity);
        return kMapping != nullptr && kMapping->acknowledged ? kMapping->id.value : 0;
    }
    [[nodiscard]] world::EntityHandle entityOf(std::uint32_t net) const noexcept override {
        const auto kFound = peer_->byNetEntity.find(net);
        return kFound != peer_->byNetEntity.end() ? kFound->second : world::EntityHandle{};
    }

private:
    const Peer* peer_;
};

} // namespace rawframe::world_replication
