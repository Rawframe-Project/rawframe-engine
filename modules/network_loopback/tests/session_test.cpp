// Sessions over loopback: admission end to end, every rejection reason a
// client can meet, timeouts, protocol violations closing the peer, and
// datagrams kept to their lanes, and strikes against malformed ones; event
// lanes carrying records in order, and every lane stream a peer may not
// open closing it (D265).

#include "rawframe/network/errors.h"
#include "rawframe/network/session.h"
#include "rawframe/network/wire.h"
#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <iterator>
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

    explicit World(std::size_t maximumAdmitted = 0, std::vector<network::EventLaneDeclaration> lanes = {}) {
        server = *network::Sessions::server(*serverTransport,
                                            clock,
                                            network::ServerSettings{.profile = kSessions,
                                                                    .expected = compatibility(),
                                                                    .admit = &ticketCheck,
                                                                    .maximumAdmitted = maximumAdmitted,
                                                                    .tickRateTicks = 30,
                                                                    .lanes = lanes,
                                                                    .seed = 11});
        client =
            *network::Sessions::client(*clientTransport, clock, {.profile = kSessions, .lanes = lanes, .seed = 12});
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

namespace {

/// Two lanes the server sends on, one small, one past a stream send, and
/// one the client sends on, numbered nought.
const std::vector<network::EventLaneDeclaration> kLanes = {{.id = 1, .fromServer = true, .maximumRecord = 256},
                                                           {.id = 2, .fromServer = true, .maximumRecord = 4096},
                                                           {.id = 0, .fromServer = false, .maximumRecord = 64}};

std::vector<std::byte> bodyOf(std::string_view text) {
    std::vector<std::byte> body;
    for (const char kCharacter : text) {
        body.push_back(static_cast<std::byte>(kCharacter));
    }
    return body;
}

/// The client's and the server's names for one admitted connection, and
/// its epoch.
struct Admitted {
    ConnectionId client;
    ConnectionId server;
    std::uint64_t epoch = 0;
};

Admitted admit(World& world) {
    const auto kConnection = world.client->connect({"game"}, hello());
    world.run(20);
    const SessionEvent* granted = world.find(world.serverEvents, SessionEventKind::Admitted);
    RAWFRAME_EXPECT(kConnection.has_value() && granted != nullptr);
    return granted == nullptr ? Admitted{}
                              : Admitted{.client = kConnection.value_or(ConnectionId{}),
                                         .server = granted->connection,
                                         .epoch = granted->accept.connectionEpoch};
}

} // namespace

RAWFRAME_TEST(EventLanesCarryRecordsInOrder) {
    World world{0, kLanes};
    // Nothing is sent before admission.
    RAWFRAME_EXPECT(!world.server->sendEvent(ConnectionId{1}, 1, 1, bodyOf("early")).has_value());
    const Admitted kPeer = admit(world);
    for (const std::string_view kWord : {"one", "two", "three"}) {
        RAWFRAME_EXPECT(world.server->sendEvent(kPeer.server, 1, kWord.size(), bodyOf(kWord)).has_value());
    }
    std::vector<std::byte> large(4096);
    for (std::size_t index = 0; index < large.size(); ++index) {
        large[index] = static_cast<std::byte>(index * 7);
    }
    RAWFRAME_EXPECT(world.server->sendEvent(kPeer.server, 2, 9, large).has_value());
    RAWFRAME_EXPECT(world.client->sendEvent(kPeer.client, 0, 4, bodyOf("back")).has_value());
    // Each side sends only on its own lanes, within their bounds.
    RAWFRAME_EXPECT(!world.server->sendEvent(kPeer.server, 0, 0, {}).has_value());
    RAWFRAME_EXPECT(!world.client->sendEvent(kPeer.client, 1, 0, {}).has_value());
    RAWFRAME_EXPECT(!world.server->sendEvent(kPeer.server, 7, 0, {}).has_value());
    RAWFRAME_EXPECT(!world.server->sendEvent(kPeer.server, 1, 0, std::vector<std::byte>(257)).has_value());
    world.run(10);
    std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> heard;
    for (const SessionEvent& event : world.clientEvents) {
        if (event.kind == SessionEventKind::Event) {
            heard.emplace_back(event.eventLane * 100 + event.payloadType, event.payload);
        }
    }
    const std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> kExpected = {
        {103, bodyOf("one")}, {103, bodyOf("two")}, {105, bodyOf("three")}, {209, large}};
    // Lane 1 in its order; lane 2 is independent of it.
    std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> firstLane;
    std::ranges::copy_if(heard, std::back_inserter(firstLane), [](const auto& each) {
        return each.first < 200;
    });
    RAWFRAME_EXPECT(heard.size() == 4 && std::ranges::is_permutation(heard, kExpected) &&
                    std::ranges::equal(firstLane, std::span{kExpected}.first(3)));
    const SessionEvent* back = world.find(world.serverEvents, SessionEventKind::Event);
    RAWFRAME_EXPECT(back != nullptr && back->eventLane == 0 && back->payloadType == 4 &&
                    back->payload == bodyOf("back"));
    RAWFRAME_EXPECT(world.find(world.serverEvents, SessionEventKind::Ended) == nullptr);
}

RAWFRAME_TEST(LaneStreamsAPeerMayNotOpenCloseIt) {
    // What a client's transport sends on a one-way stream of its own, once
    // admitted: a preface, then frames.
    const auto kViolates = [](auto write) {
        World world{0, kLanes};
        const Admitted kPeer = admit(world);
        std::vector<std::byte> bytes(512);
        network::Writer writer{bytes};
        write(writer, kPeer.epoch);
        const auto kStream = world.clientTransport->openStream(kPeer.client, true);
        RAWFRAME_EXPECT(kStream.has_value() &&
                        world.clientTransport->send(kPeer.client, *kStream, writer.written()).has_value());
        world.run(10);
        const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
        return ended != nullptr && ended->reason == network::EndReason::ProtocolViolation;
    };
    const auto kPreface = [](std::uint64_t lane, std::uint64_t epoch) {
        return network::StreamPreface{
            .kind = network::StreamKind::EventLane, .eventLaneId = lane, .eventLaneEpoch = epoch};
    };
    const auto kRecord = [](network::Writer& writer, std::uint64_t type, std::size_t bytes) {
        std::vector<std::byte> payload(bytes + 1);
        payload[0] = std::byte{1};
        RAWFRAME_EXPECT(network::writeFrame(writer, type, payload).has_value());
    };
    // Its own lane, well formed: read, not a violation.
    RAWFRAME_EXPECT(!kViolates([&](network::Writer& writer, std::uint64_t epoch) {
        RAWFRAME_EXPECT(network::writePreface(writer, kPreface(0, epoch)).has_value());
        kRecord(writer, network::kEventRecordFrame, 64);
    }));
    // A lane no one declared; the server's own lane; another epoch.
    for (const auto& [kLane, kShift] : {std::pair{9U, 0U}, std::pair{1U, 0U}, std::pair{0U, 1U}}) {
        RAWFRAME_EXPECT(kViolates([&](network::Writer& writer, std::uint64_t epoch) {
            RAWFRAME_EXPECT(network::writePreface(writer, kPreface(kLane, epoch + kShift)).has_value());
        }));
    }
    // A control preface on a one-way stream.
    RAWFRAME_EXPECT(kViolates([&](network::Writer& writer, std::uint64_t) {
        RAWFRAME_EXPECT(network::writePreface(writer, {.kind = network::StreamKind::Control}).has_value());
    }));
    // A record past the lane's bound, and a critical frame lanes do not have.
    RAWFRAME_EXPECT(kViolates([&](network::Writer& writer, std::uint64_t epoch) {
        RAWFRAME_EXPECT(network::writePreface(writer, kPreface(0, epoch)).has_value());
        kRecord(writer, network::kEventRecordFrame, 65);
    }));
    RAWFRAME_EXPECT(kViolates([&](network::Writer& writer, std::uint64_t epoch) {
        RAWFRAME_EXPECT(network::writePreface(writer, kPreface(0, epoch)).has_value());
        kRecord(writer, 2, 4);
    }));
    // A second stream for one lane.
    {
        World world{0, kLanes};
        const Admitted kPeer = admit(world);
        for (int stream = 0; stream < 2; ++stream) {
            std::vector<std::byte> bytes(64);
            network::Writer writer{bytes};
            RAWFRAME_EXPECT(network::writePreface(writer, kPreface(0, kPeer.epoch)).has_value());
            const auto kStream = world.clientTransport->openStream(kPeer.client, true);
            RAWFRAME_EXPECT(kStream.has_value() &&
                            world.clientTransport->send(kPeer.client, *kStream, writer.written()).has_value());
        }
        world.run(10);
        const SessionEvent* ended = world.find(world.serverEvents, SessionEventKind::Ended);
        RAWFRAME_EXPECT(ended != nullptr && ended->reason == network::EndReason::ProtocolViolation);
    }
    // Lanes are declared at most sixteen, each once, each with a bound.
    ManualClock clock;
    LoopbackNetwork network{clock, {}};
    auto transport = *network.provider(kTransport);
    const auto kRefused = [&](std::vector<network::EventLaneDeclaration> lanes) {
        return !network::Sessions::client(*transport, clock, {.profile = kSessions, .lanes = std::move(lanes)})
                    .has_value();
    };
    RAWFRAME_EXPECT(kRefused({{.id = 1, .maximumRecord = 8}, {.id = 1, .maximumRecord = 8}}));
    RAWFRAME_EXPECT(kRefused({{.id = 1, .maximumRecord = 0}}));
    RAWFRAME_EXPECT(kRefused({{.id = 1, .maximumRecord = network::kMaximumEventRecord + 1}}));
    std::vector<network::EventLaneDeclaration> many;
    for (std::uint64_t lane = 0; lane < 17; ++lane) {
        many.push_back({.id = lane, .maximumRecord = 8});
    }
    RAWFRAME_EXPECT(kRefused(many));
}
