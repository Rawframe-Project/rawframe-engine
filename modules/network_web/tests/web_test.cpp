// The web provider over a page's transport, here the relay the test runner
// stands in for WebTransport (tools/web_transport_relay.mjs): connecting by
// name, ordered streams and whole datagrams, every bound, a full queue, and
// closing from either side or by a provider going away.

#include "rawframe/network/errors.h"
#include "rawframe/network_web/web.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::NetworkError;
using network_web::webProvider;

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

struct Pair {
    std::unique_ptr<network::Provider> server;
    std::unique_ptr<network::Provider> client;
    ConnectionId serverSide;
    ConnectionId clientSide;
};

Pair connected(std::string_view endpoint, const network::ProviderProfile& profile = kProfile) {
    Pair pair{.server = *webProvider(profile), .client = *webProvider(profile)};
    RAWFRAME_EXPECT(pair.server->listen({std::string{endpoint}}).has_value());
    pair.clientSide = *pair.client->connect({std::string{endpoint}});
    const auto kAccepted = drain(*pair.server);
    RAWFRAME_EXPECT(kAccepted.size() == 1 && kAccepted[0].kind == EventKind::Accepted);
    pair.serverSide = kAccepted.empty() ? ConnectionId{} : kAccepted[0].connection;
    const auto kConnected = drain(*pair.client);
    RAWFRAME_EXPECT(kConnected.size() == 1 && kConnected[0].kind == EventKind::Connected &&
                    kConnected[0].connection == pair.clientSide);
    return pair;
}

} // namespace

RAWFRAME_TEST(AListenerIsReachedByName) {
    Pair pair = connected("arena");
    RAWFRAME_EXPECT(pair.clientSide.valid() && pair.serverSide.valid());
    RAWFRAME_EXPECT(failedWith(pair.client->connect({"nowhere"}), NetworkError::Unreachable));
    auto other = *webProvider(kProfile);
    RAWFRAME_EXPECT(!other->listen({"arena"}).has_value());
}

RAWFRAME_TEST(StreamsArriveInOrderAndDatagramsWhole) {
    Pair pair = connected("streams");
    const auto kStream = pair.client->openStream(pair.clientSide, false);
    RAWFRAME_EXPECT(kStream.has_value() && kStream->openedByConnector() && !kStream->unidirectional());
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, *kStream, bytes({1, 2})).has_value());
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, *kStream, bytes({3})).has_value());
    RAWFRAME_EXPECT(pair.client->sendDatagram(pair.clientSide, bytes({9, 9, 9})).has_value());
    const auto kEvents = drain(*pair.server);
    RAWFRAME_EXPECT(kEvents.size() == 3);
    if (kEvents.size() == 3) {
        RAWFRAME_EXPECT(kEvents[0].kind == EventKind::StreamBytes && kEvents[0].bytes == bytes({1, 2}) &&
                        kEvents[0].stream == *kStream);
        RAWFRAME_EXPECT(kEvents[1].kind == EventKind::StreamBytes && kEvents[1].bytes == bytes({3}));
        RAWFRAME_EXPECT(kEvents[2].kind == EventKind::Datagram && kEvents[2].bytes == bytes({9, 9, 9}));
    }
    // The server answers on the client's two-way stream, and opens its own.
    RAWFRAME_EXPECT(pair.server->send(pair.serverSide, *kStream, bytes({7})).has_value());
    const auto kOwn = pair.server->openStream(pair.serverSide, true);
    RAWFRAME_EXPECT(kOwn.has_value() && !kOwn->openedByConnector() && kOwn->unidirectional());
    RAWFRAME_EXPECT(pair.server->send(pair.serverSide, *kOwn, bytes({8})).has_value());
    const auto kAnswer = drain(*pair.client);
    RAWFRAME_EXPECT(kAnswer.size() == 2 && kAnswer[0].stream == *kStream && kAnswer[0].bytes == bytes({7}) &&
                    kAnswer[1].stream == *kOwn);
    // But not on the client's one-way stream, nor the client on the server's.
    const auto kOneWay = pair.client->openStream(pair.clientSide, true);
    RAWFRAME_EXPECT(kOneWay.has_value() && kOneWay->unidirectional() && kOneWay->openedByConnector());
    RAWFRAME_EXPECT(failedWith(pair.server->send(pair.serverSide, *kOneWay, bytes({1})), NetworkError::WrongStream));
    RAWFRAME_EXPECT(failedWith(pair.client->send(pair.clientSide, *kOwn, bytes({1})), NetworkError::WrongStream));
    const auto kStatistics = pair.server->statistics();
    RAWFRAME_EXPECT(kStatistics.datagramsDelivered == 1 && kStatistics.streamBytesDelivered == 3 &&
                    kStatistics.streamBytesSent == 2);
}

RAWFRAME_TEST(EveryBoundIsKept) {
    RAWFRAME_EXPECT(failedWith(webProvider({}), NetworkError::InvalidProfile));
    Pair pair = connected("bounds");
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    const std::vector<std::byte> kLarge(257);
    RAWFRAME_EXPECT(failedWith(pair.client->send(pair.clientSide, kStream, kLarge), NetworkError::TooLarge));
    const std::vector<std::byte> kBigDatagram(65);
    RAWFRAME_EXPECT(failedWith(pair.client->sendDatagram(pair.clientSide, kBigDatagram), NetworkError::TooLarge));
    for (int opened = 1; opened < 4; ++opened) {
        RAWFRAME_EXPECT(pair.client->openStream(pair.clientSide, true).has_value());
    }
    RAWFRAME_EXPECT(failedWith(pair.client->openStream(pair.clientSide, true), NetworkError::Exhausted));
    RAWFRAME_EXPECT(failedWith(pair.client->sendDatagram(ConnectionId{99}, bytes({1})), NetworkError::StaleConnection));

    // A listener without room turns the second peer away, which hears why.
    network::ProviderProfile one = kProfile;
    one.maximumConnections = 1;
    auto small = *webProvider(one);
    RAWFRAME_EXPECT(small->listen({"small"}).has_value());
    auto first = *webProvider(kProfile);
    auto second = *webProvider(kProfile);
    RAWFRAME_EXPECT(first->connect({"small"}).has_value());
    RAWFRAME_EXPECT(second->connect({"small"}).has_value());
    RAWFRAME_EXPECT(drain(*small).size() == 1);
    const auto kRefused = drain(*second);
    RAWFRAME_EXPECT(kRefused.size() == 2 && kRefused[1].kind == EventKind::Closed &&
                    kRefused[1].reason == network::CloseReason::Refused);
}

RAWFRAME_TEST(AFullQueueDropsDatagramsAndClosesStreams) {
    network::ProviderProfile tight = kProfile;
    tight.maximumQueuedEvents = 3;
    Pair pair = connected("tight", tight);
    for (int sent = 0; sent < 5; ++sent) {
        RAWFRAME_EXPECT(pair.client->sendDatagram(pair.clientSide, bytes({sent})).has_value());
    }
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    RAWFRAME_EXPECT(failedWith(pair.client->send(pair.clientSide, kStream, bytes({1})), NetworkError::Exhausted));
    // The connection is over on both sides, and says why.
    const auto kClient = drain(*pair.client);
    RAWFRAME_EXPECT(kClient.size() == 1 && kClient[0].kind == EventKind::Closed &&
                    kClient[0].reason == network::CloseReason::QueueExhausted);
    const auto kServer = drain(*pair.server);
    RAWFRAME_EXPECT(kServer.size() == 4 && kServer[3].kind == EventKind::Closed);
    RAWFRAME_EXPECT(failedWith(pair.client->sendDatagram(pair.clientSide, bytes({1})), NetworkError::StaleConnection));
}

RAWFRAME_TEST(ClosingReachesBothSidesAfterWhatWasSent) {
    Pair pair = connected("closing");
    const auto kStream = *pair.client->openStream(pair.clientSide, false);
    RAWFRAME_EXPECT(pair.client->send(pair.clientSide, kStream, bytes({1})).has_value());
    pair.client->close(pair.clientSide);
    const auto kLocal = drain(*pair.client);
    RAWFRAME_EXPECT(kLocal.size() == 1 && kLocal[0].kind == EventKind::Closed);
    const auto kRemote = drain(*pair.server);
    RAWFRAME_EXPECT(kRemote.size() == 2 && kRemote[0].kind == EventKind::StreamBytes &&
                    kRemote[1].kind == EventKind::Closed);
    RAWFRAME_EXPECT(failedWith(pair.server->sendDatagram(pair.serverSide, bytes({1})), NetworkError::StaleConnection));

    // A provider going away closes its connections for their peers.
    Pair again = connected("gone");
    again.server.reset();
    const auto kGone = drain(*again.client);
    RAWFRAME_EXPECT(kGone.size() == 1 && kGone[0].reason == network::CloseReason::PeerGone);
}
