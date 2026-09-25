// The loopback provider: a round trip to connect, ordered streams, datagrams
// that are lost, duplicated, and reordered by seed, every bound, closing from
// either side, and two threads using one network.

#include "rawframe/base/platform.h"
#include "rawframe/network/errors.h"
#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;
using execution::ManualClock;
using execution::MonotonicDuration;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::NetworkError;
using network_loopback::LoopbackConditions;
using network_loopback::LoopbackNetwork;

namespace {

constexpr network::ProviderProfile kProfile{.maximumConnections = 4,
                                            .maximumStreamsPerConnection = 4,
                                            .maximumStreamSend = 256,
                                            .maximumDatagram = 64,
                                            .maximumQueuedEvents = 64,
                                            .maximumQueuedBytes = 4096};

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> made;
    for (const int kValue : values) {
        made.push_back(static_cast<std::byte>(kValue));
    }
    return made;
}

std::vector<Event> drain(network::Provider& provider) {
    std::vector<Event> events;
    provider.poll(events, 1000);
    return events;
}

template <typename T> bool failedWith(const result::Result<T>& outcome, NetworkError error) {
    return !outcome.has_value() && outcome.error().code() == network::code(error);
}

/// A connected pair after the handshake, with both sides' connection IDs.
struct Pair {
    std::unique_ptr<network::Provider> server;
    std::unique_ptr<network::Provider> client;
    ConnectionId serverSide;
    ConnectionId clientSide;
};

Pair connected(LoopbackNetwork& network,
               ManualClock& clock,
               const network::ProviderProfile& profile = kProfile,
               std::string_view endpoint = "server") {
    Pair pair{.server = *network.provider(profile), .client = *network.provider(profile)};
    RAWFRAME_EXPECT(pair.server->listen({std::string{endpoint}}).has_value());
    pair.clientSide = *pair.client->connect({std::string{endpoint}});
    clock.advance(MonotonicDuration::fromMilliseconds(100));
    const auto kAccepted = drain(*pair.server);
    RAWFRAME_EXPECT(kAccepted.size() == 1 && kAccepted[0].kind == EventKind::Accepted);
    pair.serverSide = kAccepted.empty() ? ConnectionId{} : kAccepted[0].connection;
    const auto kConnected = drain(*pair.client);
    RAWFRAME_EXPECT(kConnected.size() == 1 && kConnected[0].kind == EventKind::Connected);
    return pair;
}

} // namespace

RAWFRAME_TEST(ConnectingTakesARoundTrip) {
    ManualClock clock;
    LoopbackNetwork network{clock, {.latency = MonotonicDuration::fromMilliseconds(10)}};
    auto server = *network.provider(kProfile);
    auto client = *network.provider(kProfile);
    RAWFRAME_EXPECT(server->listen({"arena"}).has_value());
    const auto kConnection = client->connect({"arena"});
    RAWFRAME_EXPECT(kConnection.has_value() && kConnection->valid());
    RAWFRAME_EXPECT(drain(*server).empty() && drain(*client).empty());
    clock.advance(MonotonicDuration::fromMilliseconds(10));
    const auto kAccepted = drain(*server);
    RAWFRAME_EXPECT(kAccepted.size() == 1 && kAccepted[0].kind == EventKind::Accepted);
    RAWFRAME_EXPECT(drain(*client).empty());
    clock.advance(MonotonicDuration::fromMilliseconds(10));
    const auto kConnected = drain(*client);
    RAWFRAME_EXPECT(kConnected.size() == 1 && kConnected[0].kind == EventKind::Connected &&
                    kConnected[0].connection == *kConnection);

    RAWFRAME_EXPECT(failedWith(client->connect({"nowhere"}), NetworkError::Unreachable));
    auto other = *network.provider(kProfile);
    RAWFRAME_EXPECT(!other->listen({"arena"}).has_value());
}

RAWFRAME_TEST(StreamsArriveInOrderAndDatagramsWhole) {
    ManualClock clock;
    LoopbackNetwork network{clock, {.latency = MonotonicDuration::fromMilliseconds(5)}};
    Pair pair = connected(network, clock);
    const auto kStream = pair.client->openStream(pair.clientSide, false);
    RAWFRAME_EXPECT(kStream.has_value() && kStream->openedByConnector() && !kStream->unidirectional());
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, *kStream, bytes({1, 2})).has_value());
    clock.advance(MonotonicDuration::fromMilliseconds(2));
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, *kStream, bytes({3})).has_value());
    RAWFRAME_EXPECT(pair.client->sendDatagram(pair.clientSide, bytes({9, 9, 9})).has_value());
    clock.advance(MonotonicDuration::fromMilliseconds(10));
    const auto kEvents = drain(*pair.server);
    RAWFRAME_EXPECT(kEvents.size() == 3);
    RAWFRAME_EXPECT(kEvents[0].kind == EventKind::StreamBytes && kEvents[0].bytes == bytes({1, 2}));
    RAWFRAME_EXPECT(kEvents[1].kind == EventKind::StreamBytes && kEvents[1].bytes == bytes({3}));
    RAWFRAME_EXPECT(kEvents[2].kind == EventKind::Datagram && kEvents[2].bytes == bytes({9, 9, 9}));
    // The server answers on the client's two-way stream.
    RAWFRAME_EXPECT(pair.server->send(pair.serverSide, *kStream, bytes({7})).has_value());
    clock.advance(MonotonicDuration::fromMilliseconds(5));
    const auto kAnswer = drain(*pair.client);
    RAWFRAME_EXPECT(kAnswer.size() == 1 && kAnswer[0].stream == *kStream && kAnswer[0].bytes == bytes({7}));
    // But not on its one-way stream.
    const auto kOneWay = pair.client->openStream(pair.clientSide, true);
    RAWFRAME_EXPECT(kOneWay.has_value() && kOneWay->unidirectional());
    RAWFRAME_EXPECT(failedWith(pair.server->send(pair.serverSide, *kOneWay, bytes({1})), NetworkError::WrongStream));
    const auto kStatistics = pair.server->statistics();
    RAWFRAME_EXPECT(kStatistics.datagramsDelivered == 1 && kStatistics.streamBytesDelivered == 3);
}

RAWFRAME_TEST(EveryBoundIsKept) {
    ManualClock clock;
    LoopbackNetwork network{clock, {}};
    RAWFRAME_EXPECT(failedWith(network.provider({}), NetworkError::InvalidProfile));
    Pair pair = connected(network, clock);
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    const std::vector<std::byte> kLarge(257);
    const auto kTooLarge = pair.client->send(pair.clientSide, kStream, kLarge);
    RAWFRAME_EXPECT(!kTooLarge.has_value() && kTooLarge.error().code() == network::code(NetworkError::TooLarge));
    const std::vector<std::byte> kBigDatagram(65);
    RAWFRAME_EXPECT(!pair.client->sendDatagram(pair.clientSide, kBigDatagram).has_value());
    for (int opened = 1; opened < 4; ++opened) {
        RAWFRAME_EXPECT(pair.client->openStream(pair.clientSide, true).has_value());
    }
    RAWFRAME_EXPECT(failedWith(pair.client->openStream(pair.clientSide, true), NetworkError::Exhausted));

    // A listener without room refuses; the connector hears after a round trip.
    network::ProviderProfile one = kProfile;
    one.maximumConnections = 1;
    auto small = *network.provider(one);
    RAWFRAME_EXPECT(small->listen({"small"}).has_value());
    auto first = *network.provider(kProfile);
    auto second = *network.provider(kProfile);
    RAWFRAME_EXPECT(first->connect({"small"}).has_value());
    RAWFRAME_EXPECT(second->connect({"small"}).has_value());
    const auto kRefused = drain(*second);
    RAWFRAME_EXPECT(kRefused.size() == 1 && kRefused[0].kind == EventKind::Closed &&
                    kRefused[0].reason == network::CloseReason::Refused);
}

RAWFRAME_TEST(AFullQueueDropsDatagramsAndClosesStreams) {
    ManualClock clock;
    LoopbackNetwork network{clock, {}};
    network::ProviderProfile tight = kProfile;
    tight.maximumQueuedEvents = 3;
    Pair pair = connected(network, clock, tight);
    for (int sent = 0; sent < 5; ++sent) {
        RAWFRAME_EXPECT(pair.client->sendDatagram(pair.clientSide, bytes({sent})).has_value());
    }
    RAWFRAME_EXPECT(pair.client->statistics().datagramsDropped == 2);
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    const auto kOverflow = pair.client->send(pair.clientSide, kStream, bytes({1}));
    RAWFRAME_EXPECT(!kOverflow.has_value() && kOverflow.error().code() == network::code(NetworkError::Exhausted));
    // The connection is over on both sides, and says why.
    const auto kClient = drain(*pair.client);
    RAWFRAME_EXPECT(kClient.size() == 1 && kClient[0].kind == EventKind::Closed &&
                    kClient[0].reason == network::CloseReason::QueueExhausted);
    const auto kServer = drain(*pair.server);
    RAWFRAME_EXPECT(kServer.size() == 4 && kServer[3].kind == EventKind::Closed);
    RAWFRAME_EXPECT(failedWith(pair.client->sendDatagram(pair.clientSide, bytes({1})), NetworkError::StaleConnection));
}

RAWFRAME_TEST(ClosingReachesBothSidesAfterWhatWasSent) {
    ManualClock clock;
    LoopbackNetwork network{clock, {.latency = MonotonicDuration::fromMilliseconds(5)}};
    Pair pair = connected(network, clock);
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, kStream, bytes({1})).has_value());
    pair.client->close(pair.clientSide);
    const auto kLocal = drain(*pair.client);
    RAWFRAME_EXPECT(kLocal.size() == 1 && kLocal[0].kind == EventKind::Closed);
    clock.advance(MonotonicDuration::fromMilliseconds(5));
    const auto kRemote = drain(*pair.server);
    RAWFRAME_EXPECT(kRemote.size() == 2 && kRemote[0].kind == EventKind::StreamBytes &&
                    kRemote[1].kind == EventKind::Closed);
    RAWFRAME_EXPECT(!pair.server->sendDatagram(pair.serverSide, bytes({1})).has_value());

    // A provider going away closes its connections for their peers.
    Pair again = connected(network, clock, kProfile, "other");
    again.server.reset();
    clock.advance(MonotonicDuration::fromMilliseconds(5));
    const auto kGone = drain(*again.client);
    RAWFRAME_EXPECT(kGone.size() == 1 && kGone[0].reason == network::CloseReason::PeerGone);
}

RAWFRAME_TEST(ConditionsFollowTheSeed) {
    const auto kRun = [](std::uint64_t seed) {
        ManualClock clock;
        LoopbackNetwork network{clock,
                                {.latency = MonotonicDuration::fromMilliseconds(20),
                                 .jitter = MonotonicDuration::fromMilliseconds(30),
                                 .datagramLossPerMillion = 200'000,
                                 .datagramDuplicatePerMillion = 100'000,
                                 .seed = seed}};
        network::ProviderProfile roomy = kProfile;
        roomy.maximumQueuedEvents = 2000;
        roomy.maximumQueuedBytes = 1 << 16;
        Pair pair = connected(network, clock, roomy);
        for (int sent = 0; sent < 1000; ++sent) {
            RAWFRAME_EXPECT(pair.client->sendDatagram(pair.clientSide, bytes({sent % 256, sent / 256})).has_value());
        }
        clock.advance(MonotonicDuration::fromMilliseconds(100));
        std::vector<int> order;
        for (const Event& event : drain(*pair.server)) {
            order.push_back(std::to_integer<int>(event.bytes[0]) + (std::to_integer<int>(event.bytes[1]) * 256));
        }
        return order;
    };
    const auto kFirst = kRun(7);
    RAWFRAME_EXPECT(kFirst == kRun(7));
    RAWFRAME_EXPECT(kFirst != kRun(8));
    // About 800 survive loss and about 80 of those are doubled; jitter
    // reorders them.
    RAWFRAME_EXPECT(kFirst.size() > 800 && kFirst.size() < 960);
    bool reordered = false;
    for (std::size_t index = 1; index < kFirst.size(); ++index) {
        reordered = reordered || kFirst[index] < kFirst[index - 1];
    }
    RAWFRAME_EXPECT(reordered);
}

#if RAWFRAME_THREADS
RAWFRAME_TEST(TwoThreadsShareOneNetwork) {
    const execution::SteadyClock kClock;
    LoopbackNetwork network{kClock, {}};
    network::ProviderProfile roomy = kProfile;
    roomy.maximumQueuedEvents = 1000;
    auto server = *network.provider(roomy);
    auto client = *network.provider(roomy);
    RAWFRAME_EXPECT(server->listen({"threads"}).has_value());
    const ConnectionId kClientSide = *client->connect({"threads"});
    constexpr int kSent = 500;
    int received = 0;
    std::thread reader{[&] {
        std::vector<Event> events;
        while (received < kSent) {
            events.clear();
            server->poll(events, 64);
            for (const Event& event : events) {
                received += event.kind == EventKind::Datagram ? 1 : 0;
            }
            if (events.empty()) {
                std::this_thread::yield();
            }
        }
    }};
    for (int sent = 0; sent < kSent; ++sent) {
        RAWFRAME_EXPECT(client->sendDatagram(kClientSide, bytes({sent % 256})).has_value());
    }
    reader.join();
    RAWFRAME_EXPECT(received == kSent && client->statistics().datagramsDropped == 0);
}
#endif
