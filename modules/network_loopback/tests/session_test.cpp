// Sessions over loopback: admission end to end, every rejection reason a
// client can meet, timeouts, protocol violations closing the peer, and
// datagrams kept to their lanes, and strikes against malformed ones.

#include "rawframe/network/errors.h"
#include "rawframe/network/session.h"
#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

using namespace rawframe;
using execution::ManualClock;
using execution::MonotonicDuration;
using network::ConnectionId;
using network::SessionEvent;
using network::SessionEventKind;
using network_loopback::LoopbackNetwork;

namespace {

constexpr network::ProviderProfile kTransport{.maximumConnections = 8,
                                              .maximumStreamsPerConnection = 4,
                                              .maximumStreamSend = 1024,
                                              .maximumDatagram = 1200,
                                              .maximumQueuedEvents = 256,
                                              .maximumQueuedBytes = 1 << 16};

constexpr network::SessionProfile kSessions{.maximumSessions = 4,
                                            .maximumPreAdmissionBytes = 2048,
                                            .maximumControlBuffer = 1 << 16,
                                            .maximumFramePayload = 4096,
                                            .maximumDatagramPayload = 1024,
                                            .admissionTimeout = MonotonicDuration{1'000'000'000}};

network::Compatibility compatibility() {
    network::Compatibility made{.protocol = network::protocolFingerprint()};
    made.game.bytes.fill(std::byte{1});
    made.schema.bytes.fill(std::byte{2});
    return made;
}

network::Hello hello() {
    return network::Hello{.compatibility = compatibility(),
                          .requestedSession = {std::byte{'a'}},
                          .ticket = {std::byte{'o'}, std::byte{'k'}},
                          .maximumDatagram = 1200,
                          .maximumFrame = 65536};
}

std::optional<network::Reject> ticketCheck(const network::Hello& offered, void*) noexcept {
    if (offered.ticket.size() == 2 && offered.ticket[0] == std::byte{'o'}) {
        return std::nullopt;
    }
    return network::Reject{.reason = network::RejectReason::TicketInvalid, .message = "no valid ticket"};
}

struct World {
    ManualClock clock;
    LoopbackNetwork network{clock, {.latency = MonotonicDuration::fromMilliseconds(10)}};
    std::unique_ptr<network::Provider> serverTransport = *network.provider(kTransport);
    std::unique_ptr<network::Provider> clientTransport = *network.provider(kTransport);
    std::unique_ptr<network::Sessions> server;
    std::unique_ptr<network::Sessions> client;
    std::vector<SessionEvent> serverEvents;
    std::vector<SessionEvent> clientEvents;

    explicit World(std::size_t maximumAdmitted = 0) {
        server = *network::Sessions::server(*serverTransport,
                                            clock,
                                            network::ServerSettings{.profile = kSessions,
                                                                    .expected = compatibility(),
                                                                    .admit = &ticketCheck,
                                                                    .maximumAdmitted = maximumAdmitted,
                                                                    .tickRateTicks = 30,
                                                                    .seed = 11});
        client = *network::Sessions::client(*clientTransport, clock, {.profile = kSessions, .seed = 12});
        RAWFRAME_EXPECT(server->listen({"game"}).has_value());
    }

    /// Advances the clock in steps, pumping both sides.
    void run(int steps) {
        for (int step = 0; step < steps; ++step) {
            clock.advance(MonotonicDuration::fromMilliseconds(5));
            server->pump(serverEvents);
            client->pump(clientEvents);
        }
    }

    const SessionEvent* find(const std::vector<SessionEvent>& events, SessionEventKind kind) const {
        for (const SessionEvent& event : events) {
            if (event.kind == kind) {
                return &event;
            }
        }
        return nullptr;
    }
};

} // namespace

RAWFRAME_TEST(AClientIsAdmitted) {
    World world;
    world.server->setTickOrigin(500);
    const auto kConnection = world.client->connect({"game"}, hello());
    RAWFRAME_EXPECT(kConnection.has_value());
    world.run(20);
    const SessionEvent* admitted = world.find(world.clientEvents, SessionEventKind::Admitted);
    const SessionEvent* granted = world.find(world.serverEvents, SessionEventKind::Admitted);
    RAWFRAME_EXPECT(admitted != nullptr && granted != nullptr);
    if (admitted == nullptr || granted == nullptr) {
        return;
    }
    RAWFRAME_EXPECT(admitted->connection == *kConnection);
    RAWFRAME_EXPECT(admitted->accept.tickRateTicks == 30 && admitted->accept.tickOrigin == 500);
    RAWFRAME_EXPECT(admitted->accept.replicationEpoch != 0 && admitted->accept.inputEpoch != 0);
    RAWFRAME_EXPECT(admitted->accept.replicationEpoch == granted->accept.replicationEpoch);
    RAWFRAME_EXPECT(admitted->accept.maximumDatagram == 1024 && admitted->accept.maximumFrame == 4096);
    RAWFRAME_EXPECT(granted->requestedSession == std::vector<std::byte>{std::byte{'a'}});

    // Admitted, both lanes carry: frames both ways, datagrams by direction.
    const std::vector<std::byte> kPayload = {std::byte{1}, std::byte{2}};
    RAWFRAME_EXPECT(world.client->sendFrame(*kConnection, network::ControlFrame::StateAck, kPayload).has_value());
    RAWFRAME_EXPECT(world.client
                        ->sendDatagram(*kConnection,
                                       {.lane = network::DatagramLane::Input,
                                        .laneEpoch = admitted->accept.inputEpoch,
                                        .sequence = 1,
                                        .payloadType = 3,
                                        .payload = kPayload})
                        .has_value());
    RAWFRAME_EXPECT(
        !world.client->sendDatagram(*kConnection, {.lane = network::DatagramLane::State, .payload = kPayload})
             .has_value());
    RAWFRAME_EXPECT(world.server
                        ->sendDatagram(granted->connection,
                                       {.lane = network::DatagramLane::State,
                                        .laneEpoch = granted->accept.replicationEpoch,
                                        .sequence = 1,
                                        .payloadType = 4,
                                        .payload = kPayload})
                        .has_value());
    world.serverEvents.clear();
    world.clientEvents.clear();
    world.run(5);
    const SessionEvent* frame = world.find(world.serverEvents, SessionEventKind::Frame);
    RAWFRAME_EXPECT(frame != nullptr &&
                    frame->frameType == static_cast<std::uint64_t>(network::ControlFrame::StateAck) &&
                    frame->payload == kPayload);
    const SessionEvent* input = world.find(world.serverEvents, SessionEventKind::Datagram);
    RAWFRAME_EXPECT(input != nullptr && input->lane == network::DatagramLane::Input && input->payloadType == 3);
    const SessionEvent* state = world.find(world.clientEvents, SessionEventKind::Datagram);
    RAWFRAME_EXPECT(state != nullptr && state->lane == network::DatagramLane::State && state->sequence == 1);

    // Closing reaches the other side.
    world.client->close(*kConnection);
    world.run(5);
    const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
    RAWFRAME_EXPECT(ended != nullptr && ended->reason == network::EndReason::Closed);
}

RAWFRAME_TEST(IncompatibleClientsAreRejectedWithTheReason) {
    const auto kReasonFor = [](auto change) {
        World world;
        network::Hello offered = hello();
        change(offered);
        RAWFRAME_EXPECT(world.client->connect({"game"}, offered).has_value());
        world.run(20);
        const SessionEvent* rejected = world.find(world.clientEvents, SessionEventKind::Rejected);
        const SessionEvent* ended = world.find(world.clientEvents, SessionEventKind::Ended);
        RAWFRAME_EXPECT(ended != nullptr && ended->reason == network::EndReason::Rejected);
        RAWFRAME_EXPECT(world.find(world.serverEvents, SessionEventKind::Admitted) == nullptr);
        return rejected != nullptr ? rejected->reject.reason : network::RejectReason::Malformed;
    };
    RAWFRAME_EXPECT(kReasonFor([](network::Hello& h) {
                        h.compatibility.schema.bytes[0] = std::byte{9};
                    }) == network::RejectReason::SchemaMismatch);
    RAWFRAME_EXPECT(kReasonFor([](network::Hello& h) {
                        h.compatibility.game.bytes[5] = std::byte{9};
                    }) == network::RejectReason::GameMismatch);
    RAWFRAME_EXPECT(kReasonFor([](network::Hello& h) {
                        h.generation = 2;
                    }) == network::RejectReason::ProtocolMismatch);
    RAWFRAME_EXPECT(kReasonFor([](network::Hello& h) {
                        h.requiredFeatures = 1;
                    }) == network::RejectReason::FeatureMissing);
    RAWFRAME_EXPECT(kReasonFor([](network::Hello& h) {
                        h.ticket.clear();
                    }) == network::RejectReason::TicketInvalid);
}

RAWFRAME_TEST(AFullServerSaysSoAndMakesRoomWhenAPlayerLeaves) {
    World world{1};
    const auto kFirst = world.client->connect({"game"}, hello());
    world.run(20);
    const auto kSecond = world.client->connect({"game"}, hello());
    world.run(20);
    RAWFRAME_EXPECT(kFirst.has_value() && kSecond.has_value());
    if (!kFirst.has_value() || !kSecond.has_value()) {
        return;
    }
    const auto kVerdict = [&world](network::ConnectionId connection, SessionEventKind kind) {
        for (const SessionEvent& event : world.clientEvents) {
            if (event.connection == connection && event.kind == kind) {
                return &event;
            }
        }
        return static_cast<const SessionEvent*>(nullptr);
    };
    RAWFRAME_EXPECT(kVerdict(*kFirst, SessionEventKind::Admitted) != nullptr);
    const SessionEvent* full = kVerdict(*kSecond, SessionEventKind::Rejected);
    RAWFRAME_EXPECT(full != nullptr && full->reject.reason == network::RejectReason::Capacity);

    // The first leaves; the next is admitted.
    world.client->close(*kFirst);
    world.run(5);
    const auto kThird = world.client->connect({"game"}, hello());
    world.run(20);
    RAWFRAME_EXPECT(kThird.has_value() && kVerdict(*kThird, SessionEventKind::Admitted) != nullptr);
}

RAWFRAME_TEST(ASilentPeerTimesOut) {
    World world;
    // A raw transport connects but never says hello.
    auto silent = *world.network.provider(kTransport);
    RAWFRAME_EXPECT(silent->connect({"game"}).has_value());
    world.run(10);
    RAWFRAME_EXPECT(world.find(world.serverEvents, SessionEventKind::Ended) == nullptr);
    world.clock.advance(MonotonicDuration::fromSeconds(1));
    world.run(1);
    const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
    RAWFRAME_EXPECT(ended != nullptr && ended->reason == network::EndReason::TimedOut);
}

RAWFRAME_TEST(ProtocolViolationsCloseThePeer) {
    const auto kSendRaw = [](std::vector<std::byte> bytes, bool unidirectional) {
        World world;
        auto raw = *world.network.provider(kTransport);
        const ConnectionId kConnection = *raw->connect({"game"});
        world.run(5);
        std::vector<network::Event> ignored;
        raw->poll(ignored, 100);
        const auto kStream = *raw->openStream(kConnection, unidirectional);
        for (std::size_t at = 0; at < bytes.size(); at += 1000) {
            const std::size_t kPart = std::min<std::size_t>(1000, bytes.size() - at);
            RAWFRAME_EXPECT(raw->send(kConnection, kStream, std::span{bytes}.subspan(at, kPart)).has_value());
        }
        world.run(5);
        const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
        return ended != nullptr && ended->reason == network::EndReason::ProtocolViolation;
    };
    // An event-lane preface where the control stream must be.
    RAWFRAME_EXPECT(kSendRaw({std::byte{1}, std::byte{1}, std::byte{0}, std::byte{0}}, false));
    // A control preface, then an accept frame from the client.
    RAWFRAME_EXPECT(kSendRaw({std::byte{0}, std::byte{1}, std::byte{2}, std::byte{0}}, false));
    // A one-way stream as the first stream.
    RAWFRAME_EXPECT(kSendRaw({std::byte{0}, std::byte{1}}, true));
    // A hello longer than any frame this side takes.
    RAWFRAME_EXPECT(kSendRaw({std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0x7f}, std::byte{0xff}}, false));
    // A hello within the frame bound is waited for, until what is held
    // passes the pre-admission bound.
    std::vector<std::byte> partial = {std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0x4f}, std::byte{0xa0}};
    partial.resize(1000, std::byte{0});
    RAWFRAME_EXPECT(!kSendRaw(partial, false));
    partial.resize(2100, std::byte{0});
    RAWFRAME_EXPECT(kSendRaw(partial, false));
}

RAWFRAME_TEST(DatagramsBeforeAdmissionAreDropped) {
    World world;
    auto raw = *world.network.provider(kTransport);
    const ConnectionId kConnection = *raw->connect({"game"});
    world.run(5);
    const std::vector<std::byte> kInput = {std::byte{0}, std::byte{1}, std::byte{1}, std::byte{0}, std::byte{0}};
    RAWFRAME_EXPECT(raw->sendDatagram(kConnection, kInput).has_value());
    world.run(5);
    RAWFRAME_EXPECT(world.find(world.serverEvents, SessionEventKind::Datagram) == nullptr);
    RAWFRAME_EXPECT(world.server->droppedDatagrams() == 1);
}

RAWFRAME_TEST(StrikesWithinTenSecondsCloseThePeer) {
    // SPEC-0013's malformed and security strikes (D223): seven unreadable
    // datagrams are dropped, and the eighth within ten seconds ends the
    // connection; strikes further apart than that never add up.
    const auto kAdmitted = [](World& world) {
        const auto kConnection = world.client->connect({"game"}, hello());
        world.run(20);
        const SessionEvent* granted = world.find(world.serverEvents, SessionEventKind::Admitted);
        RAWFRAME_EXPECT(kConnection.has_value() && granted != nullptr);
        return std::pair{kConnection.value_or(ConnectionId{}),
                         granted != nullptr ? granted->connection : ConnectionId{}};
    };
    const std::vector<std::byte> kGarbage = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    const auto kEnded = [](World& world) {
        const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
        return ended != nullptr && ended->reason == network::EndReason::ProtocolViolation;
    };
    {
        World world;
        const auto [kClient, kServer] = kAdmitted(world);
        for (std::size_t strike = 1; strike < network::kMaximumStrikes; ++strike) {
            RAWFRAME_EXPECT(world.clientTransport->sendDatagram(kClient, kGarbage).has_value());
            world.run(5);
        }
        RAWFRAME_EXPECT(!kEnded(world) && world.server->struckOut() == 0);
        RAWFRAME_EXPECT(world.clientTransport->sendDatagram(kClient, kGarbage).has_value());
        world.run(5);
        RAWFRAME_EXPECT(kEnded(world) && world.server->struckOut() == 1);
        static_cast<void>(kServer);
    }
    {
        World world;
        const auto [kClient, kServer] = kAdmitted(world);
        for (std::size_t strike = 0; strike < 3 * network::kMaximumStrikes; ++strike) {
            RAWFRAME_EXPECT(world.clientTransport->sendDatagram(kClient, kGarbage).has_value());
            // Strikes a second and a half apart: any eight span more than
            // ten seconds.
            world.run(300);
        }
        RAWFRAME_EXPECT(!kEnded(world) && world.server->struckOut() == 0);
        static_cast<void>(kServer);
    }
    {
        // The owner's strikes count the same, and the one that strikes out
        // closes without an `Ended`: the owner forgets the connection.
        World world;
        const auto [kClient, kServer] = kAdmitted(world);
        for (std::size_t strike = 1; strike < network::kMaximumStrikes; ++strike) {
            RAWFRAME_EXPECT(world.server->strike(kServer));
        }
        RAWFRAME_EXPECT(!world.server->strike(kServer) && world.server->struckOut() == 1);
        RAWFRAME_EXPECT(!world.server->strike(kServer));
        world.run(5);
        RAWFRAME_EXPECT(world.find(world.serverEvents, SessionEventKind::Ended) == nullptr);
        const SessionEvent* gone = world.find(world.clientEvents, SessionEventKind::Ended);
        RAWFRAME_EXPECT(gone != nullptr);
        static_cast<void>(kClient);
    }
}
