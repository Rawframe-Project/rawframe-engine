// A join over QUIC in view of the most entities a connection may hold: the
// server declares them within its mapping budget a publish, so neither its
// transport's sending bound nor the client's 256 KiB unread ever fills,
// whether MsQuic buffers sends or not, and the client mirrors every one.
// The transport and session bounds are replication's own (profiles.h).

#include "rawframe/network_quic/certificate.h"
#include "rawframe/network_quic/quic.h"
#include "rawframe/world_replication/messages.h"
#include "rawframe/world_replication/profiles.h"
#include "replication_scenario.h"

#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>

using namespace rawframe;
using namespace rawframe::scenario;

namespace {

/// A port this scenario listens on: tried in turn until one listens, as a
/// port another program holds is refused, and a provider says no port it
/// chose itself.
std::uint16_t listenOnAnyPort(network::Sessions& sessions) {
    for (std::uint16_t port = 41'000; port < 41'200; ++port) {
        if (sessions.listen({"127.0.0.1:" + std::to_string(port)}).has_value()) {
            return port;
        }
    }
    return 0;
}

/// The time from admission until the client has taken every mapping, or
/// none when the join did not complete.
std::optional<std::chrono::milliseconds> joinInViewOfTheMost(bool buffered) {
    const auto kIdentity = *network_quic::makeSelfSignedCertificate("rawframe-test", 1);
    auto serverNetwork = network_quic::QuicNetwork::create(
        network_quic::QuicSettings{.certificate = kIdentity, .sendBuffering = buffered});
    auto clientNetwork = network_quic::QuicNetwork::create(
        network_quic::QuicSettings{.pin = *network_quic::fingerprintOf(kIdentity), .sendBuffering = buffered});
    RAWFRAME_EXPECT(serverNetwork.has_value() && clientNetwork.has_value());
    if (!serverNetwork.has_value() || !clientNetwork.has_value()) {
        return std::nullopt;
    }
    auto serverTransport = *(*serverNetwork)->provider(world_replication::providerProfile(2));
    auto clientTransport = *(*clientNetwork)->provider(world_replication::providerProfile(1));
    const execution::SteadyClock kClock;
    const std::vector<network::EventLaneDeclaration> kLanes = {world_replication::engineLane()};

    const auto kSchema = registry();
    world::World serverWorld{kSchema};
    world::World clientWorld{kSchema};
    auto serverSessions = *network::Sessions::server(
        *serverTransport,
        kClock,
        {.profile = world_replication::sessionProfile(2), .expected = compatibility(), .lanes = kLanes});
    const std::uint16_t kPort = listenOnAnyPort(*serverSessions);
    RAWFRAME_EXPECT(kPort != 0);
    auto server = *world_replication::ReplicationServer::create(
        *serverSessions,
        {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
         .playerComponents = {Position::kComponentTypeId, Steer::kComponentTypeId},
         .input = steerCodec(),
         .maximumMapped = world_replication::kMostMapped});
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(server->declareSystems(*kSchema, declarations).has_value());
    auto schedule = *world::Schedule::compile(declarations, *kSchema);
    // Everything in view: the player is the last of kMostMapped.
    const auto kPosition = *kSchema->key<Position>();
    for (std::size_t index = 0; index + 1 < world_replication::kMostMapped; ++index) {
        const world::EntityHandle kProp = *serverWorld.create();
        static_cast<void>(serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index), 0}));
    }

    auto clientSessions = *network::Sessions::client(
        *clientTransport, kClock, {.profile = world_replication::sessionProfile(1), .lanes = kLanes});
    auto client = *world_replication::ReplicationClient::create(
        *clientSessions,
        clientWorld,
        {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
         .input = steerCodec(),
         .maximumMapped = world_replication::kMostMapped});
    RAWFRAME_EXPECT(
        client
            ->connect({"127.0.0.1:" + std::to_string(kPort)},
                      network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
            .has_value());

    // Each declaration the client takes is an entity it mirrors, before any
    // state for it arrives (which the state budget paces apart).
    const auto kMirrored = [&clientWorld] {
        return clientWorld.entityCount();
    };
    // Ticks at 60 a second, as long as the machine lets them; the client
    // pumps between them as a client's frames do.
    world::TickIndex tick;
    std::optional<std::chrono::steady_clock::time_point> admitted;
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    while (std::chrono::steady_clock::now() < kDeadline && !client->ended()) {
        const auto kTickStart = std::chrono::steady_clock::now();
        server->pump(serverWorld, tick);
        RAWFRAME_EXPECT(schedule.runTick(serverWorld, tick, *world::TickRate::of(60)).has_value());
        client->pump();
        if (client->admitted()) {
            if (!admitted.has_value()) {
                admitted = std::chrono::steady_clock::now();
            }
            const Steer kStill{};
            static_cast<void>(client->submitInput(std::as_bytes(std::span{&kStill, 1})));
            if (kMirrored() == world_replication::kMostMapped) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                             *admitted);
            }
        }
        std::this_thread::sleep_until(kTickStart + std::chrono::microseconds{16'667});
    }
    std::fprintf(stderr,
                 "join %s: ended %d, mirrored %zu of %zu\n",
                 buffered ? "buffered" : "unbuffered",
                 client->ended() ? 1 : 0,
                 kMirrored(),
                 world_replication::kMostMapped);
    return std::nullopt;
}

} // namespace

RAWFRAME_TEST(AJoinInViewOfTheMostEntitiesKeepsItsConnectionUnbuffered) {
    const auto kTook = joinInViewOfTheMost(false);
    RAWFRAME_EXPECT(kTook.has_value());
    if (kTook.has_value()) {
        std::fprintf(stderr,
                     "join unbuffered: every mapping taken %lld ms after admission\n",
                     static_cast<long long>(kTook->count()));
    }
}

RAWFRAME_TEST(AJoinInViewOfTheMostEntitiesKeepsItsConnectionBuffered) {
    const auto kTook = joinInViewOfTheMost(true);
    RAWFRAME_EXPECT(kTook.has_value());
    if (kTook.has_value()) {
        std::fprintf(stderr,
                     "join buffered: every mapping taken %lld ms after admission\n",
                     static_cast<long long>(kTook->count()));
    }
}
