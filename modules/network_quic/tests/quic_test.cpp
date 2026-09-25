// The QUIC provider over this machine's loopback interface: identities and
// pins, the contract's events in both directions, a pin that does not match,
// a listener with no room, and admission through Sessions on top.

#include "rawframe/network/errors.h"
#include "rawframe/network/session.h"
#include "rawframe/network_quic/errors.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/test/test.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using network::CloseReason;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network_quic::QuicNetwork;
using network_quic::QuicSettings;

namespace {

constexpr network::ProviderProfile kProfile{.maximumConnections = 4,
                                            .maximumStreamsPerConnection = 4,
                                            .maximumStreamSend = 4096,
                                            .maximumDatagram = 1200,
                                            .maximumQueuedEvents = 256,
                                            .maximumQueuedBytes = 1 << 16};

/// A UDP port nothing holds right now, from the kernel.
std::uint16_t freePort() {
    const int kSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(address);
    ::bind(kSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::getsockname(kSocket, reinterpret_cast<sockaddr*>(&address), &length);
    ::close(kSocket);
    return ntohs(address.sin_port);
}

std::string endpointAt(std::uint16_t port) {
    return "127.0.0.1:" + std::to_string(port);
}

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

/// Polls every provider until `done` holds or five seconds pass.
bool pumpUntil(std::initializer_list<std::pair<network::Provider*, std::vector<Event>*>> sides,
               const std::function<bool()>& done) {
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < kDeadline) {
        for (const auto& [provider, events] : sides) {
            provider->poll(*events, 64);
        }
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

const Event* find(const std::vector<Event>& events, EventKind kind) {
    for (const Event& event : events) {
        if (event.kind == kind) {
            return &event;
        }
    }
    return nullptr;
}

struct Pair {
    network_quic::Certificate identity = *network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    std::unique_ptr<QuicNetwork> server = *QuicNetwork::create(QuicSettings{.certificate = identity});
    std::unique_ptr<QuicNetwork> client =
        *QuicNetwork::create(QuicSettings{.pin = *network_quic::fingerprintOf(identity)});
    std::uint16_t port = freePort();
};

} // namespace

RAWFRAME_TEST(IdentitiesAndFingerprints) {
    const auto kFirst = network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    const auto kSecond = network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    RAWFRAME_EXPECT(kFirst.has_value() && kSecond.has_value());
    if (!kFirst.has_value() || !kSecond.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kFirst->certificatePem.starts_with("-----BEGIN CERTIFICATE-----"));
    const auto kPrint = network_quic::fingerprintOf(*kFirst);
    RAWFRAME_EXPECT(kPrint.has_value() && *kPrint == *network_quic::fingerprintOf(*kFirst));
    RAWFRAME_EXPECT(*kPrint != *network_quic::fingerprintOf(*kSecond));
    const std::string kText = network_quic::formatFingerprint(*kPrint);
    RAWFRAME_EXPECT(kText.size() == 64 && *network_quic::parseFingerprint(kText) == *kPrint);
    RAWFRAME_EXPECT(!network_quic::parseFingerprint(kText.substr(1)).has_value());
    RAWFRAME_EXPECT(!network_quic::fingerprintOf({.certificatePem = "not a certificate"}).has_value());
    RAWFRAME_EXPECT(!network_quic::makeSelfSignedCertificate("", 1).has_value());

    // A key that is not the certificate's is refused when the network loads it.
    network_quic::Certificate mixed = *kFirst;
    mixed.privateKeyPem = kSecond->privateKeyPem;
    const auto kMixed = QuicNetwork::create(QuicSettings{.certificate = mixed});
    RAWFRAME_EXPECT(!kMixed.has_value() && kMixed.error().code() == code(network_quic::QuicError::BadCertificate));
}

RAWFRAME_TEST(TheContractOverQuic) {
    Pair pair;
    auto server = *pair.server->provider(kProfile);
    auto client = *pair.client->provider(kProfile);
    RAWFRAME_EXPECT(server->listen({endpointAt(pair.port)}).has_value());
    RAWFRAME_EXPECT(!client->listen({endpointAt(freePort())}).has_value());
    RAWFRAME_EXPECT(!server->connect({endpointAt(pair.port)}).has_value());

    const auto kConnection = client->connect({endpointAt(pair.port)});
    RAWFRAME_EXPECT(kConnection.has_value());
    std::vector<Event> serverEvents;
    std::vector<Event> clientEvents;
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}, {client.get(), &clientEvents}}, [&] {
        return find(serverEvents, EventKind::Accepted) != nullptr &&
               find(clientEvents, EventKind::Connected) != nullptr;
    }));
    const Event* accepted = find(serverEvents, EventKind::Accepted);
    if (accepted == nullptr || !kConnection.has_value()) {
        return;
    }
    const ConnectionId kPeer = accepted->connection;
    RAWFRAME_EXPECT(find(clientEvents, EventKind::Connected)->connection == *kConnection);

    // A stream opened by the connector is number 0 on both sides.
    const auto kStream = client->openStream(*kConnection, false);
    RAWFRAME_EXPECT(kStream.has_value() && kStream->value == 0);
    RAWFRAME_EXPECT(client->send(*kConnection, *kStream, bytesOf("hello")).has_value());
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}}, [&] {
        return find(serverEvents, EventKind::StreamBytes) != nullptr;
    }));
    const Event* heard = find(serverEvents, EventKind::StreamBytes);
    RAWFRAME_EXPECT(heard != nullptr && heard->stream.value == 0 && heard->bytes == bytesOf("hello"));
    RAWFRAME_EXPECT(server->send(kPeer, network::StreamId{0}, bytesOf("world")).has_value());
    RAWFRAME_EXPECT(pumpUntil({{client.get(), &clientEvents}}, [&] {
        return find(clientEvents, EventKind::StreamBytes) != nullptr;
    }));
    const Event* answer = find(clientEvents, EventKind::StreamBytes);
    RAWFRAME_EXPECT(answer != nullptr && answer->bytes == bytesOf("world"));
    RAWFRAME_EXPECT(!client->send(*kConnection, network::StreamId{4}, bytesOf("x")).has_value());
    RAWFRAME_EXPECT(!client->send(*kConnection, *kStream, std::vector<std::byte>(4097)).has_value());

    // Datagrams, some of which the first few may lose.
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}}, [&] {
        static_cast<void>(client->sendDatagram(*kConnection, bytesOf("ping")));
        return find(serverEvents, EventKind::Datagram) != nullptr;
    }));
    const Event* datagram = find(serverEvents, EventKind::Datagram);
    RAWFRAME_EXPECT(datagram != nullptr && datagram->bytes == bytesOf("ping"));
    RAWFRAME_EXPECT(!client->sendDatagram(*kConnection, std::vector<std::byte>(1201)).has_value());
    RAWFRAME_EXPECT(client->statistics().datagramsSent >= 1 && server->statistics().datagramsDelivered >= 1);
    RAWFRAME_EXPECT(server->statistics().streamBytesDelivered == 5);

    // Closing ends both sides, each told once.
    client->close(*kConnection);
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}, {client.get(), &clientEvents}}, [&] {
        return find(serverEvents, EventKind::Closed) != nullptr && find(clientEvents, EventKind::Closed) != nullptr;
    }));
    const Event* serverClosed = find(serverEvents, EventKind::Closed);
    const Event* clientClosed = find(clientEvents, EventKind::Closed);
    RAWFRAME_EXPECT(serverClosed != nullptr && serverClosed->reason == CloseReason::Closed);
    RAWFRAME_EXPECT(clientClosed != nullptr && clientClosed->reason == CloseReason::Closed);
    RAWFRAME_EXPECT(!client->send(*kConnection, *kStream, bytesOf("late")).has_value());
}

RAWFRAME_TEST(AnotherCertificateIsNotTrusted) {
    Pair pair;
    const network_quic::Certificate kImpostor = *network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    auto impostor = *QuicNetwork::create(QuicSettings{.certificate = kImpostor});
    auto server = *impostor->provider(kProfile);
    auto client = *pair.client->provider(kProfile);
    RAWFRAME_EXPECT(server->listen({endpointAt(pair.port)}).has_value());
    const auto kConnection = client->connect({endpointAt(pair.port)});
    std::vector<Event> serverEvents;
    std::vector<Event> clientEvents;
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}, {client.get(), &clientEvents}}, [&] {
        return find(clientEvents, EventKind::Closed) != nullptr;
    }));
    const Event* closed = find(clientEvents, EventKind::Closed);
    RAWFRAME_EXPECT(closed != nullptr && closed->reason == CloseReason::Untrusted);
    RAWFRAME_EXPECT(find(clientEvents, EventKind::Connected) == nullptr);
    RAWFRAME_EXPECT(find(serverEvents, EventKind::Accepted) == nullptr);
}

RAWFRAME_TEST(AFullListenerRefuses) {
    Pair pair;
    network::ProviderProfile single = kProfile;
    single.maximumConnections = 1;
    auto server = *pair.server->provider(single);
    auto first = *pair.client->provider(kProfile);
    auto second = *pair.client->provider(kProfile);
    RAWFRAME_EXPECT(server->listen({endpointAt(pair.port)}).has_value());
    RAWFRAME_EXPECT(first->connect({endpointAt(pair.port)}).has_value());
    std::vector<Event> serverEvents;
    std::vector<Event> firstEvents;
    std::vector<Event> secondEvents;
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}, {first.get(), &firstEvents}}, [&] {
        return find(firstEvents, EventKind::Connected) != nullptr;
    }));
    RAWFRAME_EXPECT(second->connect({endpointAt(pair.port)}).has_value());
    RAWFRAME_EXPECT(pumpUntil({{server.get(), &serverEvents}, {second.get(), &secondEvents}}, [&] {
        return find(secondEvents, EventKind::Closed) != nullptr;
    }));
    const Event* refused = find(secondEvents, EventKind::Closed);
    RAWFRAME_EXPECT(refused != nullptr && refused->reason == CloseReason::Refused);

    // A provider going away tells its peers it is gone.
    server.reset();
    RAWFRAME_EXPECT(pumpUntil({{first.get(), &firstEvents}}, [&] {
        return find(firstEvents, EventKind::Closed) != nullptr;
    }));
    const Event* gone = find(firstEvents, EventKind::Closed);
    RAWFRAME_EXPECT(gone != nullptr && gone->reason == CloseReason::PeerGone);
}

RAWFRAME_TEST(SessionsAdmitOverQuic) {
    Pair pair;
    auto serverTransport = *pair.server->provider(kProfile);
    auto clientTransport = *pair.client->provider(kProfile);
    const execution::SteadyClock kClock;
    constexpr network::SessionProfile kSessions{.maximumSessions = 4,
                                                .maximumPreAdmissionBytes = 2048,
                                                .maximumControlBuffer = 1 << 16,
                                                .maximumFramePayload = 4096,
                                                .maximumDatagramPayload = 1024,
                                                .admissionTimeout = execution::MonotonicDuration::fromSeconds(5)};
    network::Compatibility compatibility{.protocol = network::protocolFingerprint()};
    compatibility.game.bytes.fill(std::byte{1});
    compatibility.schema.bytes.fill(std::byte{2});
    auto server = *network::Sessions::server(
        *serverTransport, kClock, network::ServerSettings{.profile = kSessions, .expected = compatibility});
    auto client = *network::Sessions::client(*clientTransport, kClock, {.profile = kSessions});
    RAWFRAME_EXPECT(server->listen({endpointAt(pair.port)}).has_value());
    RAWFRAME_EXPECT(client
                        ->connect({endpointAt(pair.port)},
                                  network::Hello{.compatibility = compatibility,
                                                 .requestedSession = {std::byte{'a'}},
                                                 .maximumDatagram = 1200,
                                                 .maximumFrame = 65536})
                        .has_value());
    std::vector<network::SessionEvent> serverEvents;
    std::vector<network::SessionEvent> clientEvents;
    const auto kAdmitted = [](const std::vector<network::SessionEvent>& events) {
        for (const network::SessionEvent& event : events) {
            if (event.kind == network::SessionEventKind::Admitted) {
                return &event;
            }
        }
        return static_cast<const network::SessionEvent*>(nullptr);
    };
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < kDeadline &&
           (kAdmitted(serverEvents) == nullptr || kAdmitted(clientEvents) == nullptr)) {
        server->pump(serverEvents);
        client->pump(clientEvents);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const network::SessionEvent* granted = kAdmitted(serverEvents);
    const network::SessionEvent* admitted = kAdmitted(clientEvents);
    RAWFRAME_EXPECT(granted != nullptr && admitted != nullptr);
    if (granted != nullptr && admitted != nullptr) {
        // Epochs from the secure source, the same on both sides.
        RAWFRAME_EXPECT(admitted->accept.replicationEpoch == granted->accept.replicationEpoch);
        RAWFRAME_EXPECT(admitted->accept.replicationEpoch != 0);
    }
}

RAWFRAME_TEST(ABrowserReachesTheServerOverWebTransport) {
    // aioquic, an independent HTTP/3 and WebTransport implementation, plays
    // the browser (D172): it opens a session, sends on a stream and as a
    // datagram, and the owner answers each way, as on any connection.
    const auto kIdentity = *network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    auto network = QuicNetwork::create(QuicSettings{.certificate = kIdentity, .webTransport = true});
    RAWFRAME_EXPECT(network.has_value());
    if (!network.has_value()) {
        return;
    }
    auto server = *(*network)->provider(kProfile);
    const std::uint16_t kPort = freePort();
    RAWFRAME_EXPECT(server->listen({endpointAt(kPort)}).has_value());
    std::string said;
    std::thread browser{[&said, kPort] {
        const std::string kCommand = "python3 " RAWFRAME_WEBTRANSPORT_CLIENT " " + std::to_string(kPort) + " 2>&1";
        if (std::FILE* output = ::popen(kCommand.c_str(), "r")) {
            char buffer[256];
            while (std::fgets(buffer, sizeof buffer, output) != nullptr) {
                said += buffer;
            }
            said += ::pclose(output) == 0 ? "" : "(failed)";
        }
    }};
    std::vector<Event> events;
    ConnectionId browserSide;
    bool answeredStream = false;
    bool answeredDatagram = false;
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < kDeadline && !(answeredStream && answeredDatagram)) {
        events.clear();
        server->poll(events, 64);
        for (const Event& event : events) {
            if (event.kind == EventKind::Accepted) {
                browserSide = event.connection;
            } else if (event.kind == EventKind::StreamBytes && event.bytes == bytesOf("ping")) {
                RAWFRAME_EXPECT(server->send(browserSide, event.stream, bytesOf("pong")).has_value());
                const auto kOwn = server->openStream(browserSide, true);
                RAWFRAME_EXPECT(kOwn.has_value() && server->send(browserSide, *kOwn, bytesOf("hello")).has_value());
                answeredStream = true;
            } else if (event.kind == EventKind::Datagram && event.bytes == bytesOf("dgram")) {
                RAWFRAME_EXPECT(server->sendDatagram(browserSide, bytesOf("back")).has_value());
                answeredDatagram = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    browser.join();
    RAWFRAME_EXPECT(browserSide.valid() && answeredStream && answeredDatagram);
    RAWFRAME_EXPECT(said.find("webtransport: answered") != std::string::npos);
    if (said.find("webtransport: answered") == std::string::npos) {
        std::fprintf(stderr, "%s\n", said.c_str());
    }
}
