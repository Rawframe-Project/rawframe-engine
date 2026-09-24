// A server World and a client World over loopback: the client is admitted,
// gets a player, mirrors every replicated entity, drives its player with
// input, and sees exactly the server's committed values, late but never wrong,
// through loss and reordering.

#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/server.h"

#include <map>
#include <vector>

using namespace rawframe;
using execution::ManualClock;
using execution::MonotonicDuration;
using world_replication::ComponentCodec;
using world_replication::WireKind;

namespace {

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("6a1e2f3c-9d84-4b57-a0c6-2e7f1b9d3c85");
    static constexpr std::string_view kComponentName = "scenario.position";
    float x = 0;
    float y = 0;
};

struct Steer {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("c7b3d9e1-5f20-4a86-9e4b-6d1a8c2f7e39");
    static constexpr std::string_view kComponentName = "scenario.steer";
    float dx = 0;
    float dy = 0;
};

ComponentCodec positionCodec() {
    return {.component = Position::kComponentTypeId,
            .size = sizeof(Position),
            .fields = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}}};
}

ComponentCodec steerCodec() {
    return {.component = Steer::kComponentTypeId,
            .size = sizeof(Steer),
            .fields = {{offsetof(Steer, dx), WireKind::F32}, {offsetof(Steer, dy), WireKind::F32}}};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Position>().add<Steer>();
    return *builder.freeze();
}

/// Moves everything that steers.
class Move final : public world::System {
public:
    explicit Move(const schema::SchemaRegistry& registry)
        : query_(*world::Query<world::Write<Position>, world::Read<Steer>>::resolve(registry)) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        query_.forEach(context.world, [](world::EntityHandle, Position& position, const Steer& steer) {
            position.x += steer.dx;
            position.y += steer.dy;
        });
        return {};
    }
    std::vector<schema::ComponentRuntimeId> reads = {};
    std::vector<schema::ComponentRuntimeId> writes = {};

private:
    world::Query<world::Write<Position>, world::Read<Steer>> query_;
};

constexpr network::ProviderProfile kTransport{.maximumConnections = 8,
                                              .maximumStreamsPerConnection = 4,
                                              .maximumStreamSend = 1024,
                                              .maximumDatagram = 1200,
                                              .maximumQueuedEvents = 4096,
                                              .maximumQueuedBytes = 1 << 20};

constexpr network::SessionProfile kSessions{.maximumSessions = 8,
                                            .maximumPreAdmissionBytes = 4096,
                                            .maximumControlBuffer = 1 << 16,
                                            .maximumFramePayload = 4096,
                                            .maximumDatagramPayload = 1100,
                                            .admissionTimeout = MonotonicDuration{2'000'000'000}};

network::Compatibility compatibility() {
    network::Compatibility made{.protocol = network::protocolFingerprint()};
    made.game.bytes.fill(std::byte{7});
    return made;
}

/// Everything on both sides of one connection.
struct Scenario {
    ManualClock clock;
    network_loopback::LoopbackNetwork network;
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();

    world::World serverWorld{schema};
    std::unique_ptr<network::Provider> serverTransport = *network.provider(kTransport);
    std::unique_ptr<network::Sessions> serverSessions;
    std::unique_ptr<world_replication::ReplicationServer> server;
    std::unique_ptr<Move> move;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;
    /// The server's committed player position after each tick.
    std::map<std::uint64_t, Position> history;

    world::World clientWorld{schema};
    std::unique_ptr<network::Provider> clientTransport = *network.provider(kTransport);
    std::unique_ptr<network::Sessions> clientSessions;
    std::unique_ptr<world_replication::ReplicationClient> client;

    explicit Scenario(network_loopback::LoopbackConditions conditions) : network(clock, conditions) {
        serverSessions = *network::Sessions::server(
            *serverTransport, clock, {.profile = kSessions, .expected = compatibility(), .seed = 1});
        RAWFRAME_EXPECT(serverSessions->listen({"server"}).has_value());
        server = *world_replication::ReplicationServer::create(
            *serverSessions,
            {.table = {.components = {positionCodec(), steerCodec()}},
             .playerComponents = {Position::kComponentTypeId, Steer::kComponentTypeId},
             .input = steerCodec()});
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(server->declareSystems(*schema, declarations).has_value());
        move = std::make_unique<Move>(*schema);
        declarations.push_back(world::SystemDeclaration{.identity = "scenario.move", .system = move.get()});
        schedule.emplace(*world::Schedule::compile(declarations, *schema));

        clientSessions = *network::Sessions::client(*clientTransport, clock, {.profile = kSessions, .seed = 2});
        client = *world_replication::ReplicationClient::create(
            *clientSessions,
            clientWorld,
            {.table = {.components = {positionCodec(), steerCodec()}}, .input = steerCodec()});
        RAWFRAME_EXPECT(
            client
                ->connect(
                    {"server"},
                    network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
                .has_value());
    }

    /// One 16 ms step: the server pumps and ticks, the client pumps and steers.
    void step(Steer steer) {
        clock.advance(MonotonicDuration::fromMilliseconds(16));
        server->pump(serverWorld, tick);
        const std::uint64_t kRan = tick.value;
        RAWFRAME_EXPECT(schedule->runTick(serverWorld, tick, *world::TickRate::of(60)).has_value());
        if (const auto* player = firstPlayerPosition()) {
            history[kRan] = *player;
        }
        client->pump();
        if (client->admitted()) {
            RAWFRAME_EXPECT(client->submitInput(std::as_bytes(std::span{&steer, 1})).has_value());
        }
    }

    const Position* firstPlayerPosition() {
        const world::EntityHandle kPlayer = server->player(network::ConnectionId{1});
        return kPlayer.isNull() ? nullptr : serverWorld.get(kPlayer, *schema->key<Position>());
    }

    std::size_t mirrored() {
        std::size_t count = 0;
        auto query = world::Query<world::Read<Position>>::resolve(*schema);
        query->forEach(clientWorld, [&count](world::EntityHandle, const Position&) {
            ++count;
        });
        return count;
    }
};

} // namespace

RAWFRAME_TEST(AClientMirrorsTheServerAndDrivesItsPlayer) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    // Three things in the World besides players.
    const auto kPosition = *scenario.schema->key<Position>();
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 3; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(props.back(), kPosition, Position{static_cast<float>(index * 10), 0})
                .has_value());
    }
    for (int step = 0; step < 60; ++step) {
        scenario.step(Steer{1, 0.5F});
    }
    RAWFRAME_EXPECT(scenario.client->admitted());
    RAWFRAME_EXPECT(scenario.mirrored() == 4);
    const world::EntityHandle kOwned = scenario.client->owned();
    RAWFRAME_EXPECT(!kOwned.isNull());
    const Position* mirror = scenario.clientWorld.get(kOwned, kPosition);
    RAWFRAME_EXPECT(mirror != nullptr && mirror->x > 10);
    // The mirror is exactly what the server committed at the tick it says.
    const auto kCommitted = scenario.history.find(scenario.client->serverTick());
    RAWFRAME_EXPECT(kCommitted != scenario.history.end() && mirror != nullptr && kCommitted->second.x == mirror->x &&
                    kCommitted->second.y == mirror->y);
    // Pacing puts the client's input ahead of consumption within a few ticks
    // of admission, and from then on every tick consumes a real command.
    RAWFRAME_EXPECT(scenario.server->statistics().inputsConsumed > 40);
    // Props that never move are sent until acknowledged, then left out.
    RAWFRAME_EXPECT(scenario.server->statistics().recordsHeld > 100);

    // A prop destroyed on the server is retired on the client.
    RAWFRAME_EXPECT(scenario.serverWorld.destroy(props[1]).has_value());
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.mirrored() == 3);
}

RAWFRAME_TEST(ReplicationHoldsThroughLossAndReordering) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(30),
                       .jitter = MonotonicDuration::fromMilliseconds(40),
                       .datagramLossPerMillion = 250'000,
                       .datagramDuplicatePerMillion = 100'000,
                       .seed = 99}};
    // Still props: sent until a datagram carrying them is acknowledged, so a
    // lost one is sent again, and then never again.
    const auto kPosition = *scenario.schema->key<Position>();
    for (int index = 0; index < 3; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index * 10), 7}).has_value());
    }
    for (int step = 0; step < 120; ++step) {
        scenario.step(Steer{1, 0});
    }
    RAWFRAME_EXPECT(scenario.mirrored() == 4);
    int still = 0;
    auto query = world::Query<world::Read<Position>>::resolve(*scenario.schema);
    query->forEach(scenario.clientWorld, [&still](world::EntityHandle, const Position& position) {
        still += position.y == 7 && (position.x == 0 || position.x == 10 || position.x == 20) ? 1 : 0;
    });
    RAWFRAME_EXPECT(still == 3);
    RAWFRAME_EXPECT(scenario.server->statistics().recordsHeld > 0);
    const world::EntityHandle kOwned = scenario.client->owned();
    RAWFRAME_EXPECT(!kOwned.isNull());
    const Position* mirror = scenario.clientWorld.get(kOwned, *scenario.schema->key<Position>());
    const auto kCommitted = scenario.history.find(scenario.client->serverTick());
    RAWFRAME_EXPECT(mirror != nullptr && kCommitted != scenario.history.end() && kCommitted->second.x == mirror->x);
    // Redundant input windows survive a quarter of datagrams lost.
    const auto kServer = scenario.server->statistics();
    RAWFRAME_EXPECT(kServer.inputsConsumed > kServer.inputsNeutral);
    RAWFRAME_EXPECT(scenario.client->statistics().recordsStale > 0 || scenario.client->statistics().stateDatagrams > 0);
}
