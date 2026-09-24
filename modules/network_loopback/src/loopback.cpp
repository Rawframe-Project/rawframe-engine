#include "rawframe/network_loopback/loopback.h"

#include "rawframe/network/errors.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::network_loopback {

using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::NetworkError;
using network::StreamId;

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, NetworkError error, std::string_view why) {
    return result::fail(errorClass, network::kNetworkDomain, network::code(error), why);
}

struct Side;

/// One end of a connection.
struct Link {
    Side* peer = nullptr;
    ConnectionId peerId;
    bool connector = false;
    bool open = true;
    std::size_t streamsOpened = 0;
    /// Stream bytes arrive in the order sent: none before the last one.
    std::int64_t lastArrival = 0;
    /// What waits for this side's owner on this connection.
    std::size_t queuedEvents = 0;
    std::size_t queuedBytes = 0;
};

/// One provider's part of the network.
struct Side {
    network::ProviderProfile profile;
    std::uint64_t nextConnection = 1;
    std::map<std::uint64_t, Link> links;
    /// Events on their way to this side, by arrival time then sending order.
    std::map<std::pair<std::int64_t, std::uint64_t>, Event> inbound;
    std::string endpoint;
    network::ProviderStatistics statistics;
};

std::size_t openLinks(const Side& side) noexcept {
    std::size_t count = 0;
    for (const auto& [id, link] : side.links) {
        count += link.open ? 1 : 0;
    }
    return count;
}

} // namespace

struct LoopbackNetwork::State {
    std::mutex mutex;
    const execution::MonotonicSource* clock = nullptr;
    LoopbackConditions conditions;
    std::uint64_t random = 0;
    std::uint64_t order = 0;
    std::map<std::string, Side*, std::less<>> listeners;
    std::size_t sides = 0;

    [[nodiscard]] std::int64_t now() const noexcept {
        return clock->now().nanoseconds;
    }

    /// SplitMix64: a draw per decision, in call order.
    std::uint64_t draw() noexcept {
        random += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = random;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        return mixed ^ (mixed >> 31U);
    }

    bool chance(std::uint32_t perMillion) noexcept {
        return perMillion != 0 && draw() % 1'000'000U < perMillion;
    }

    /// Whether `to` can hold one more event of `bytes` for `connection`.
    static bool fits(const Side& to, const Link& link, std::size_t bytes) noexcept {
        return link.queuedEvents + 1 <= to.profile.maximumQueuedEvents &&
               link.queuedBytes + bytes <= to.profile.maximumQueuedBytes;
    }

    void deliver(Side& to, ConnectionId connection, std::int64_t at, Event event) {
        if (const auto kLink = to.links.find(connection.value); kLink != to.links.end()) {
            kLink->second.queuedEvents += 1;
            kLink->second.queuedBytes += event.bytes.size();
        }
        event.connection = connection;
        to.inbound.emplace(std::pair{at, order++}, std::move(event));
    }

    /// Ends both ends of a link now: this side learns at once, the peer after
    /// the latency and after every stream byte already on its way.
    void end(Side& side, std::uint64_t id, network::CloseReason local, network::CloseReason remote) {
        Link& link = side.links.at(id);
        if (!link.open) {
            return;
        }
        link.open = false;
        deliver(side, ConnectionId{id}, now(), Event{.kind = EventKind::Closed, .reason = local});
        if (link.peer != nullptr) {
            Link& other = link.peer->links.at(link.peerId.value);
            if (other.open) {
                other.open = false;
                const std::int64_t kAt = std::max(now() + conditions.latency.nanoseconds, other.lastArrival);
                deliver(*link.peer, link.peerId, kAt, Event{.kind = EventKind::Closed, .reason = remote});
            }
            other.peer = nullptr;
            link.peer = nullptr;
        }
    }
};

namespace {

class LoopbackProvider final : public network::Provider {
public:
    LoopbackProvider(std::shared_ptr<LoopbackNetwork::State> state, const network::ProviderProfile& profile)
        : state_(std::move(state)) {
        side_.profile = profile;
    }

    ~LoopbackProvider() override {
        const std::lock_guard kLock{state_->mutex};
        for (auto& [id, link] : side_.links) {
            if (link.open) {
                state_->end(side_, id, network::CloseReason::Closed, network::CloseReason::PeerGone);
            }
        }
        if (!side_.endpoint.empty()) {
            state_->listeners.erase(side_.endpoint);
        }
        --state_->sides;
    }

    result::Status listen(const network::Endpoint& endpoint) override {
        const std::lock_guard kLock{state_->mutex};
        if (endpoint.name.empty() || !side_.endpoint.empty() || state_->listeners.contains(endpoint.name)) {
            return refuse(result::ErrorClass::AlreadyExists,
                          NetworkError::Unreachable,
                          "the endpoint is empty or taken, or this provider already listens");
        }
        side_.endpoint = endpoint.name;
        state_->listeners.emplace(endpoint.name, &side_);
        return {};
    }

    result::Result<ConnectionId> connect(const network::Endpoint& endpoint) override {
        const std::lock_guard kLock{state_->mutex};
        const auto kListener = state_->listeners.find(endpoint.name);
        if (kListener == state_->listeners.end()) {
            return refuse(result::ErrorClass::Unavailable, NetworkError::Unreachable, "nothing listens there");
        }
        if (openLinks(side_) >= side_.profile.maximumConnections) {
            return refuse(
                result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "this provider has no room to connect");
        }
        Side& server = *kListener->second;
        const ConnectionId kMine{side_.nextConnection++};
        const std::int64_t kLatency = state_->conditions.latency.nanoseconds;
        const std::int64_t kNow = state_->now();
        Link& mine = side_.links[kMine.value];
        mine.connector = true;
        if (openLinks(server) >= server.profile.maximumConnections) {
            mine.open = false;
            state_->deliver(side_,
                            kMine,
                            kNow + (2 * kLatency),
                            Event{.kind = EventKind::Closed, .reason = network::CloseReason::Refused});
            return kMine;
        }
        const ConnectionId kTheirs{server.nextConnection++};
        Link& theirs = server.links[kTheirs.value];
        theirs.peer = &side_;
        theirs.peerId = kMine;
        mine.peer = &server;
        mine.peerId = kTheirs;
        // A round trip: the listener hears of it after one latency, and the
        // connector is ready after two.
        state_->deliver(server, kTheirs, kNow + kLatency, Event{.kind = EventKind::Accepted});
        state_->deliver(side_, kMine, kNow + (2 * kLatency), Event{.kind = EventKind::Connected});
        return kMine;
    }

    result::Result<StreamId> openStream(ConnectionId connection, bool unidirectional) override {
        const std::lock_guard kLock{state_->mutex};
        RAWFRAME_TRY_ASSIGN(Link * link, openLink(connection));
        if (link->streamsOpened >= side_.profile.maximumStreamsPerConnection) {
            return refuse(
                result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "the connection has no more streams");
        }
        // QUIC numbering: who opened it in the low bit, one-way in the next.
        const std::uint64_t kId = (static_cast<std::uint64_t>(link->streamsOpened) << 2U) | (unidirectional ? 2U : 0U) |
                                  (link->connector ? 0U : 1U);
        ++link->streamsOpened;
        return StreamId{kId};
    }

    result::Status send(ConnectionId connection, StreamId stream, std::span<const std::byte> bytes) override {
        const std::lock_guard kLock{state_->mutex};
        RAWFRAME_TRY_ASSIGN(Link * link, openLink(connection));
        if (bytes.size() > side_.profile.maximumStreamSend) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a stream send is too large");
        }
        const bool kMine = stream.openedByConnector() == link->connector;
        if (!kMine && stream.unidirectional()) {
            return refuse(
                result::ErrorClass::InvalidArgument, NetworkError::WrongStream, "the peer's one-way stream is its own");
        }
        if (link->peer == nullptr) {
            return {};
        }
        Side& peer = *link->peer;
        Link& other = peer.links.at(link->peerId.value);
        if (!LoopbackNetwork::State::fits(peer, other, bytes.size())) {
            // A reliable stream cannot drop bytes, so the connection ends.
            state_->end(
                side_, connection.value, network::CloseReason::QueueExhausted, network::CloseReason::QueueExhausted);
            return refuse(result::ErrorClass::ResourceExhausted,
                          NetworkError::Exhausted,
                          "the peer's queue is full; the connection was closed");
        }
        const std::int64_t kAt = std::max(state_->now() + state_->conditions.latency.nanoseconds, other.lastArrival);
        other.lastArrival = kAt;
        side_.statistics.streamBytesSent += bytes.size();
        state_->deliver(peer,
                        link->peerId,
                        kAt,
                        Event{.kind = EventKind::StreamBytes, .stream = stream, .bytes = {bytes.begin(), bytes.end()}});
        return {};
    }

    result::Status sendDatagram(ConnectionId connection, std::span<const std::byte> bytes) override {
        const std::lock_guard kLock{state_->mutex};
        RAWFRAME_TRY_ASSIGN(Link * link, openLink(connection));
        if (bytes.size() > side_.profile.maximumDatagram) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a datagram is too large");
        }
        ++side_.statistics.datagramsSent;
        const LoopbackConditions& conditions = state_->conditions;
        if (link->peer == nullptr || state_->chance(conditions.datagramLossPerMillion)) {
            ++side_.statistics.datagramsDropped;
            return {};
        }
        const int kCopies = state_->chance(conditions.datagramDuplicatePerMillion) ? 2 : 1;
        for (int copy = 0; copy < kCopies; ++copy) {
            Side& peer = *link->peer;
            Link& other = peer.links.at(link->peerId.value);
            if (!LoopbackNetwork::State::fits(peer, other, bytes.size())) {
                ++side_.statistics.datagramsDropped;
                continue;
            }
            const std::int64_t kJitter =
                conditions.jitter.nanoseconds > 0
                    ? static_cast<std::int64_t>(state_->draw() %
                                                (static_cast<std::uint64_t>(conditions.jitter.nanoseconds) + 1U))
                    : 0;
            state_->deliver(peer,
                            link->peerId,
                            state_->now() + conditions.latency.nanoseconds + kJitter,
                            Event{.kind = EventKind::Datagram, .bytes = {bytes.begin(), bytes.end()}});
        }
        return {};
    }

    void close(ConnectionId connection) noexcept override {
        const std::lock_guard kLock{state_->mutex};
        const auto kLink = side_.links.find(connection.value);
        if (kLink != side_.links.end() && kLink->second.open) {
            state_->end(side_, connection.value, network::CloseReason::Closed, network::CloseReason::Closed);
        }
    }

    std::size_t poll(std::vector<Event>& into, std::size_t maximum) override {
        const std::lock_guard kLock{state_->mutex};
        const std::int64_t kNow = state_->now();
        std::size_t moved = 0;
        while (moved < maximum && !side_.inbound.empty() && side_.inbound.begin()->first.first <= kNow) {
            auto node = side_.inbound.extract(side_.inbound.begin());
            Event& event = node.mapped();
            const auto kLink = side_.links.find(event.connection.value);
            if (kLink == side_.links.end()) {
                // The owner has already been told this connection closed.
                continue;
            }
            kLink->second.queuedEvents -= 1;
            kLink->second.queuedBytes -= event.bytes.size();
            if (event.kind == EventKind::Datagram) {
                ++side_.statistics.datagramsDelivered;
            } else if (event.kind == EventKind::StreamBytes) {
                side_.statistics.streamBytesDelivered += event.bytes.size();
            } else if (event.kind == EventKind::Closed) {
                side_.links.erase(kLink);
            }
            into.push_back(std::move(event));
            ++moved;
        }
        return moved;
    }

    network::ProviderStatistics statistics() const noexcept override {
        const std::lock_guard kLock{state_->mutex};
        return side_.statistics;
    }

private:
    result::Result<Link*> openLink(ConnectionId connection) {
        const auto kLink = side_.links.find(connection.value);
        if (kLink == side_.links.end() || !kLink->second.open) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          NetworkError::StaleConnection,
                          "the connection is closed or not this provider's");
        }
        return &kLink->second;
    }

    std::shared_ptr<LoopbackNetwork::State> state_;
    Side side_;
};

} // namespace

LoopbackNetwork::LoopbackNetwork(const execution::MonotonicSource& clock, LoopbackConditions conditions)
    : state_(std::make_shared<State>()) {
    state_->clock = &clock;
    state_->conditions = conditions;
    state_->random = conditions.seed;
}

LoopbackNetwork::~LoopbackNetwork() = default;

result::Result<std::unique_ptr<network::Provider>> LoopbackNetwork::provider(const network::ProviderProfile& profile) {
    if (profile.maximumConnections == 0 || profile.maximumStreamsPerConnection == 0 || profile.maximumStreamSend == 0 ||
        profile.maximumDatagram == 0 || profile.maximumQueuedEvents == 0 || profile.maximumQueuedBytes == 0) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::InvalidProfile,
                      "every provider bound is required and none may be zero");
    }
    {
        const std::lock_guard kLock{state_->mutex};
        ++state_->sides;
    }
    return std::unique_ptr<network::Provider>{new LoopbackProvider{state_, profile}};
}

} // namespace rawframe::network_loopback
