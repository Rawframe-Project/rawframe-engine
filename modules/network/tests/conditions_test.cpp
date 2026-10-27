// A provider under link conditions: sends held the latency before the
// inner provider has them, arrivals held as long, streams kept in order, and
// datagrams lost and doubled as seeded.

#include "rawframe/network/conditions.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace {

using namespace rawframe;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::StreamId;

/// A provider that keeps what it is given and hands out what a test puts
/// in it.
class Kept final : public network::Provider {
public:
    struct Sent {
        bool datagram = false;
        std::vector<std::byte> bytes;
    };
    std::vector<Sent>* sent = nullptr;
    std::vector<Event>* arriving = nullptr;
    int* closed = nullptr;

    result::Status listen(const network::Endpoint&) override {
        return {};
    }
    result::Result<ConnectionId> connect(const network::Endpoint&) override {
        return ConnectionId{1};
    }
    result::Result<StreamId> openStream(ConnectionId, bool) override {
        return StreamId{4};
    }
    result::Status send(ConnectionId, StreamId, std::span<const std::byte> bytes) override {
        sent->push_back({false, {bytes.begin(), bytes.end()}});
        return {};
    }
    std::size_t pendingBytes(ConnectionId, StreamId) const noexcept override {
        return 0;
    }
    result::Status sendDatagram(ConnectionId, std::span<const std::byte> bytes) override {
        sent->push_back({true, {bytes.begin(), bytes.end()}});
        return {};
    }
    void close(ConnectionId) noexcept override {
        ++*closed;
    }
    std::size_t poll(std::vector<Event>& into, std::size_t maximum) override {
        std::size_t moved = 0;
        while (moved < maximum && !arriving->empty()) {
            into.push_back(std::move(arriving->front()));
            arriving->erase(arriving->begin());
            ++moved;
        }
        return moved;
    }
    network::ProviderStatistics statistics() const noexcept override {
        return {};
    }
};

constexpr network::ProviderProfile kProfile{.maximumConnections = 4,
                                            .maximumStreamsPerConnection = 4,
                                            .maximumStreamSend = 1024,
                                            .maximumDatagram = 512,
                                            .maximumQueuedEvents = 64,
                                            .maximumQueuedBytes = 65536};

std::vector<std::byte> bytesOf(std::byte value) {
    return {value, value};
}

} // namespace

RAWFRAME_TEST(StreamsWaitTheLatencyAndKeepTheirOrder) {
    execution::ManualClock clock;
    std::vector<Kept::Sent> sent;
    std::vector<Event> arriving;
    int closed = 0;
    auto inner = std::make_unique<Kept>();
    inner->sent = &sent;
    inner->arriving = &arriving;
    inner->closed = &closed;
    const auto kLink = network::withConditions(
        std::move(inner), {.latency = execution::MonotonicDuration::fromMilliseconds(50)}, kProfile, clock);
    std::vector<Event> polled;
    RAWFRAME_EXPECT(kLink->send(ConnectionId{1}, StreamId{4}, bytesOf(std::byte{1})).has_value());
    clock.advance(execution::MonotonicDuration::fromMilliseconds(10));
    RAWFRAME_EXPECT(kLink->send(ConnectionId{1}, StreamId{4}, bytesOf(std::byte{2})).has_value());
    kLink->close(ConnectionId{1});
    // Held, and counted as the stream's pending bytes.
    kLink->poll(polled, 8);
    RAWFRAME_EXPECT(sent.empty() && kLink->pendingBytes(ConnectionId{1}, StreamId{4}) == 4);
    clock.advance(execution::MonotonicDuration::fromMilliseconds(40));
    kLink->poll(polled, 8);
    RAWFRAME_EXPECT(sent.size() == 1 && sent[0].bytes[0] == std::byte{1} && closed == 0);
    clock.advance(execution::MonotonicDuration::fromMilliseconds(10));
    kLink->poll(polled, 8);
    // The close after the bytes before it.
    RAWFRAME_EXPECT(sent.size() == 2 && sent[1].bytes[0] == std::byte{2} && closed == 1);
    RAWFRAME_EXPECT(kLink->pendingBytes(ConnectionId{1}, StreamId{4}) == 0);
}

RAWFRAME_TEST(ArrivalsWaitTheLatency) {
    execution::ManualClock clock;
    std::vector<Kept::Sent> sent;
    std::vector<Event> arriving;
    int closed = 0;
    auto inner = std::make_unique<Kept>();
    inner->sent = &sent;
    inner->arriving = &arriving;
    inner->closed = &closed;
    const auto kLink = network::withConditions(
        std::move(inner), {.latency = execution::MonotonicDuration::fromMilliseconds(30)}, kProfile, clock);
    arriving.push_back(Event{.kind = EventKind::Connected, .connection = ConnectionId{1}});
    arriving.push_back(
        Event{.kind = EventKind::StreamBytes, .connection = ConnectionId{1}, .bytes = bytesOf(std::byte{7})});
    std::vector<Event> polled;
    RAWFRAME_EXPECT(kLink->poll(polled, 8) == 0 && arriving.empty());
    clock.advance(execution::MonotonicDuration::fromMilliseconds(29));
    RAWFRAME_EXPECT(kLink->poll(polled, 8) == 0);
    clock.advance(execution::MonotonicDuration::fromMilliseconds(1));
    RAWFRAME_EXPECT(kLink->poll(polled, 1) == 1 && polled[0].kind == EventKind::Connected);
    RAWFRAME_EXPECT(kLink->poll(polled, 8) == 1 && polled[1].bytes[0] == std::byte{7});
}

RAWFRAME_TEST(DatagramsAreLostAndDoubledAsSeeded) {
    const auto kRun = [](std::uint64_t seed) {
        execution::ManualClock clock;
        std::vector<Kept::Sent> sent;
        std::vector<Event> arriving;
        int closed = 0;
        auto inner = std::make_unique<Kept>();
        inner->sent = &sent;
        inner->arriving = &arriving;
        inner->closed = &closed;
        const auto kLink = network::withConditions(std::move(inner),
                                                   {.latency = execution::MonotonicDuration::fromMilliseconds(5),
                                                    .jitter = execution::MonotonicDuration::fromMilliseconds(20),
                                                    .datagramLossPerMillion = 250'000,
                                                    .datagramDuplicatePerMillion = 250'000,
                                                    .seed = seed},
                                                   kProfile,
                                                   clock);
        for (int each = 0; each < 400; ++each) {
            static_cast<void>(kLink->sendDatagram(ConnectionId{1}, bytesOf(static_cast<std::byte>(each))));
        }
        std::vector<Event> polled;
        clock.advance(execution::MonotonicDuration::fromMilliseconds(30));
        kLink->poll(polled, 8);
        std::vector<std::byte> order;
        for (const Kept::Sent& kSent : sent) {
            order.push_back(kSent.bytes[0]);
        }
        return std::pair{order, kLink->statistics().datagramsDropped};
    };
    const auto [kOrder, kDropped] = kRun(7);
    // About a quarter lost, about a quarter of the rest doubled, and some
    // reordered by the jitter; the same seed the same again.
    RAWFRAME_EXPECT(kDropped > 60 && kDropped < 140);
    RAWFRAME_EXPECT(kOrder.size() > 320 && kOrder.size() < 440);
    RAWFRAME_EXPECT(!std::is_sorted(kOrder.begin(), kOrder.end()));
    const auto kAgain = kRun(7);
    RAWFRAME_EXPECT(kAgain.first == kOrder && kAgain.second == kDropped && kRun(8).first != kOrder);
}
