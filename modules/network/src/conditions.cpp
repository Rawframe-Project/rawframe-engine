#include "rawframe/network/conditions.h"

#include "rawframe/network/errors.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::network {

namespace {

/// What is held on its way out: a stream's bytes, a datagram, or a close,
/// each given to the inner provider once its time has come.
struct Held {
    enum class Kind : std::uint8_t {
        Stream,
        Datagram,
        Close
    };
    Kind kind = Kind::Stream;
    std::int64_t at = 0;
    ConnectionId connection;
    StreamId stream;
    std::vector<std::byte> bytes;
};

class Conditioned final : public Provider {
public:
    Conditioned(std::unique_ptr<Provider> inner,
                const LinkConditions& conditions,
                const ProviderProfile& profile,
                const execution::MonotonicSource& clock)
        : inner_(std::move(inner)), conditions_(conditions), mostHeld_(profile.maximumQueuedEvents), clock_(&clock),
          random_(conditions.seed) {
    }

    result::Status listen(const Endpoint& endpoint) override {
        return inner_->listen(endpoint);
    }
    result::Result<ConnectionId> connect(const Endpoint& endpoint) override {
        return inner_->connect(endpoint);
    }
    result::Result<StreamId> openStream(ConnectionId connection, bool unidirectional) override {
        return inner_->openStream(connection, unidirectional);
    }

    result::Status send(ConnectionId connection, StreamId stream, std::span<const std::byte> bytes) override {
        // In order behind what this stream holds already: a stream's bytes
        // all wait the one latency, so they leave as they came.
        out_.push_back(Held{.kind = Held::Kind::Stream,
                            .at = now() + conditions_.latency.nanoseconds,
                            .connection = connection,
                            .stream = stream,
                            .bytes = {bytes.begin(), bytes.end()}});
        held_[{connection.value, stream.value}] += bytes.size();
        return {};
    }

    std::size_t pendingBytes(ConnectionId connection, StreamId stream) const noexcept override {
        const auto kHeld = held_.find({connection.value, stream.value});
        return inner_->pendingBytes(connection, stream) + (kHeld != held_.end() ? kHeld->second : 0);
    }

    result::Status sendDatagram(ConnectionId connection, std::span<const std::byte> bytes) override {
        ++statistics_.datagramsSent;
        if (chance(conditions_.datagramLossPerMillion)) {
            ++statistics_.datagramsDropped;
            return {};
        }
        const int kCopies = chance(conditions_.datagramDuplicatePerMillion) ? 2 : 1;
        for (int copy = 0; copy < kCopies; ++copy) {
            datagrams_.emplace(
                std::pair{now() + conditions_.latency.nanoseconds + jitter(), order_++},
                Held{.kind = Held::Kind::Datagram, .connection = connection, .bytes = {bytes.begin(), bytes.end()}});
        }
        return {};
    }

    void close(ConnectionId connection) noexcept override {
        // After every stream byte already held for it, as a link would.
        out_.push_back(
            Held{.kind = Held::Kind::Close, .at = now() + conditions_.latency.nanoseconds, .connection = connection});
    }

    std::size_t poll(std::vector<Event>& into, std::size_t maximum) override {
        const std::int64_t kNow = now();
        giveOn(kNow);
        // What arrived, held the latency before it is seen; a datagram's
        // with its jitter.
        // No more than the profile's queue holds: past it, what arrives
        // waits in the inner provider, whose own bounds then hold.
        scratch_.clear();
        if (in_.size() < mostHeld_) {
            inner_->poll(scratch_, mostHeld_ - in_.size());
        }
        for (Event& event : scratch_) {
            const std::int64_t kExtra = event.kind == EventKind::Datagram ? jitter() : 0;
            in_.emplace(std::pair{kNow + conditions_.latency.nanoseconds + kExtra, order_++}, std::move(event));
        }
        std::size_t moved = 0;
        while (moved < maximum && !in_.empty() && in_.begin()->first.first <= kNow) {
            into.push_back(std::move(in_.begin()->second));
            in_.erase(in_.begin());
            ++moved;
        }
        return moved;
    }

    ProviderStatistics statistics() const noexcept override {
        ProviderStatistics made = inner_->statistics();
        made.datagramsDropped += statistics_.datagramsDropped;
        return made;
    }

private:
    [[nodiscard]] std::int64_t now() const noexcept {
        return clock_->now().nanoseconds;
    }

    /// SplitMix64, as loopback's network draws: one draw per decision, in
    /// call order, so a seed repeats a run.
    std::uint64_t draw() noexcept {
        random_ += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = random_;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        return mixed ^ (mixed >> 31U);
    }

    bool chance(std::uint32_t perMillion) noexcept {
        return perMillion != 0 && draw() % 1'000'000U < perMillion;
    }

    std::int64_t jitter() noexcept {
        const std::int64_t kMost = conditions_.jitter.nanoseconds;
        return kMost > 0 ? static_cast<std::int64_t>(draw() % static_cast<std::uint64_t>(kMost + 1)) : 0;
    }

    /// Gives the inner provider what has waited long enough: streams and
    /// closes in the order they were asked, datagrams by when they are due.
    void giveOn(std::int64_t now) {
        while (!out_.empty() && out_.front().at <= now) {
            Held& next = out_.front();
            if (next.kind == Held::Kind::Close) {
                inner_->close(next.connection);
            } else {
                auto& held = held_[{next.connection.value, next.stream.value}];
                held -= std::min(held, next.bytes.size());
                // A stream the peer closed meanwhile refuses; its bytes are
                // lost with the connection, as they would be.
                static_cast<void>(inner_->send(next.connection, next.stream, next.bytes));
            }
            out_.pop_front();
        }
        while (!datagrams_.empty() && datagrams_.begin()->first.first <= now) {
            const Held& kNext = datagrams_.begin()->second;
            static_cast<void>(inner_->sendDatagram(kNext.connection, kNext.bytes));
            datagrams_.erase(datagrams_.begin());
        }
    }

    std::unique_ptr<Provider> inner_;
    LinkConditions conditions_;
    std::size_t mostHeld_ = 0;
    const execution::MonotonicSource* clock_ = nullptr;
    std::uint64_t random_ = 0;
    std::uint64_t order_ = 0;
    std::deque<Held> out_;
    std::map<std::pair<std::int64_t, std::uint64_t>, Held> datagrams_;
    std::map<std::pair<std::int64_t, std::uint64_t>, Event> in_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> held_;
    std::vector<Event> scratch_;
    ProviderStatistics statistics_;
};

} // namespace

std::unique_ptr<Provider> withConditions(std::unique_ptr<Provider> inner,
                                         const LinkConditions& conditions,
                                         const ProviderProfile& profile,
                                         const execution::MonotonicSource& clock) {
    return std::make_unique<Conditioned>(std::move(inner), conditions, profile, clock);
}

result::Result<LinkConditions> conditionsOf(const composition::Configuration& configuration, std::string_view prefix) {
    const std::string kPrefix{prefix};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLatency, configuration.unsignedInteger(kPrefix + ".latency_ms", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kJitter, configuration.unsignedInteger(kPrefix + ".jitter_ms", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLoss, configuration.unsignedInteger(kPrefix + ".loss_ppm", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kDuplicate, configuration.unsignedInteger(kPrefix + ".duplicate_ppm", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeed, configuration.unsignedInteger(kPrefix + ".seed", 0));
    constexpr std::uint64_t kMaximumMilliseconds = 60'000;
    if (kLatency > kMaximumMilliseconds || kJitter > kMaximumMilliseconds || kLoss > 1'000'000 ||
        kDuplicate > 1'000'000) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kNetworkDomain,
                            code(NetworkError::InvalidProfile),
                            "a link condition is out of range: a minute at most, a million parts at most");
    }
    return LinkConditions{.latency =
                              execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kLatency)),
                          .jitter = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kJitter)),
                          .datagramLossPerMillion = static_cast<std::uint32_t>(kLoss),
                          .datagramDuplicatePerMillion = static_cast<std::uint32_t>(kDuplicate),
                          .seed = kSeed};
}

} // namespace rawframe::network
