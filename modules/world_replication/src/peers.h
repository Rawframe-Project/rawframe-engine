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
/// the heap was a cache miss each time.
class HeldBytes {
public:
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return size_ <= kInPlace ? std::span<const std::byte>{inPlace_.data(), size_} : std::span{heap_};
    }
    /// Bytes on the heap: none for a value held in place.
    [[nodiscard]] std::size_t heapBytes() const noexcept {
        return heap_.capacity();
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
    /// The tick it was found gone or out of interest while its retirement
    /// waits for room in a publish: it is sent nothing more, named by
    /// nothing, and counts as seen until then alone, though the client
    /// holds it until the retirement arrives.
    std::optional<std::uint64_t> retiringSince;
    /// The tick its first state record went out, for the victim gate.
    std::optional<std::uint64_t> firstSent;
    /// By replication table index.
    std::vector<Replica> replicas;
};

/// A connection's mappings by entity, in entity order. A sorted vector, not
/// a map: every tick walks them all twice, and a map's scattered nodes made
/// that walk most of a connection's cost.
class Mappings {
public:
    using Entry = std::pair<world::EntityHandle, Mapping>;

    [[nodiscard]] std::vector<Entry>::iterator begin() noexcept {
        return entries_.begin();
    }
    [[nodiscard]] std::vector<Entry>::iterator end() noexcept {
        return entries_.end();
    }
    [[nodiscard]] std::vector<Entry>::const_iterator begin() const noexcept {
        return entries_.begin();
    }
    [[nodiscard]] std::vector<Entry>::const_iterator end() const noexcept {
        return entries_.end();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }
    [[nodiscard]] std::size_t capacity() const noexcept {
        return entries_.capacity();
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
    /// Removes the mappings of `gone`, given in entity order, those it
    /// holds. One pass from the first: erasing them one by one moved every
    /// mapping after each, thousands for a dug block in a large land.
    void erase(std::span<const world::EntityHandle> gone) {
        if (gone.empty()) {
            return;
        }
        auto into = std::ranges::lower_bound(entries_, gone.front(), {}, &Entry::first);
        auto next = gone.begin();
        for (auto at = into; at != entries_.end(); ++at) {
            while (next != gone.end() && *next < at->first) {
                ++next;
            }
            if (next != gone.end() && *next == at->first) {
                continue;
            }
            if (into != at) {
                *into = std::move(*at);
            }
            ++into;
        }
        entries_.erase(into, entries_.end());
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

/// What one connection sent on the input lane in the current second, held
/// to the input ceilings before any work on a record.
struct InputAllowance {
    std::uint64_t second = 0;
    std::uint64_t windows = 0;
    std::uint64_t bytes = 0;

    /// Whether a record of `size` payload bytes may be looked at, at `tick`,
    /// with `windows` of it counting against `maximumWindows`: an input
    /// window counts one, anything else none.
    [[nodiscard]] bool admit(std::uint64_t tick,
                             std::uint64_t ticksPerSecond,
                             std::size_t size,
                             std::uint64_t window,
                             std::uint64_t maximumWindows,
                             std::uint64_t maximumBytes) noexcept {
        const std::uint64_t kSecond = tick / (ticksPerSecond == 0 ? 1 : ticksPerSecond);
        if (kSecond != second) {
            second = kSecond;
            windows = 0;
            bytes = 0;
        }
        if (bytes + size > maximumBytes || windows + window > maximumWindows) {
            return false;
        }
        bytes += size;
        windows += window;
        return true;
    }
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
    /// The publish its values were last looked at in, and the mapped
    /// entities not all of whose values it was then known to hold: what
    /// its next publish looks at, with what changed since.
    std::optional<std::uint64_t> lookedAt;
    std::vector<world::EntityHandle> unsettled;
    /// Whether the bound on mappings left an entity undeclared, or a
    /// mapping unretired, at its last turn: its next looks at every entity
    /// again.
    bool bounded = false;
    /// Which of the game's districts are in its interest, by their place,
    /// as of the districts' version.
    std::vector<bool> unitsIn;
    std::uint64_t unitsVersion = 0;
    std::map<std::uint32_t, world::EntityHandle> byNetEntity;
    /// The next input tick to consume, and commands waiting for theirs.
    std::uint64_t nextInputTick = 0;
    std::uint64_t consumedInputTick = 0;
    std::map<std::uint64_t, Waiting> waitingInputs;
    std::vector<std::byte> lastCommand;
    std::uint32_t held = 0;
    std::uint64_t stateSequence = 0;
    std::deque<SentState> sent;
    /// The newest command's tick heard, and how far ahead of the tick being
    /// consumed it stood at the least since the last pace: what a
    /// client whose commands come in bursts is late by just before the next.
    std::uint64_t newestHeard = 0;
    std::optional<std::int64_t> leastLead;
    bool heardInput = false;
    /// The server tick its last input window arrived in, and the least lead
    /// since, counted only once the next window comes within a command's
    /// age: a client unheard for longer is stalled or cut off, and is
    /// not paced for it.
    std::uint64_t heardAt = 0;
    std::optional<std::int64_t> pendingLead;
    ChecksumBook checksums;
    InputAllowance allowance;
    /// The tick its last command arrived before, and how many it has sent
    /// for that tick.
    std::uint64_t commandsTick = 0;
    std::size_t commandsThisTick = 0;
    /// Closed by this side, for breaking the protocol or striking out: to
    /// be forgotten once its event is handled.
    bool gone = false;
    /// Told the server is stopping.
    bool toldStopping = false;
    /// What its connection was sent and sent, as the server's totals count
    /// them, for a tool's profile.
    std::uint64_t stateDatagrams = 0;
    std::uint64_t stateBytes = 0;
    std::uint64_t recordsSent = 0;
    std::uint64_t recordsDeferred = 0;
    std::uint64_t inputWindows = 0;
    std::uint64_t inputBytes = 0;
};

/// An entity by the ID of the mapping a connection has acknowledged for it:
/// what it may be named by in a value sent there.
class PeerNames final : public EntityNames {
public:
    explicit PeerNames(const Peer& peer) noexcept : peer_(&peer) {
    }
    [[nodiscard]] std::uint32_t netOf(world::EntityHandle entity) const noexcept override {
        const Mapping* const kMapping = peer_->mapped.find(entity);
        return kMapping != nullptr && kMapping->acknowledged && !kMapping->retiringSince.has_value()
                   ? kMapping->id.value
                   : 0;
    }
    [[nodiscard]] world::EntityHandle entityOf(std::uint32_t net) const noexcept override {
        const auto kFound = peer_->byNetEntity.find(net);
        if (kFound == peer_->byNetEntity.end()) {
            return {};
        }
        // A retiring mapping names nothing, as a retired one does not.
        const Mapping* const kMapping = peer_->mapped.find(kFound->second);
        return kMapping != nullptr && !kMapping->retiringSince.has_value() ? kFound->second : world::EntityHandle{};
    }

private:
    const Peer* peer_;
};

} // namespace rawframe::world_replication
