// A server World and a client World over loopback: the client is admitted,
// gets a player, mirrors every replicated entity, drives its player with
// input, and sees exactly the server's committed values, late but never wrong,
// through loss and reordering, and only what is in its interest.

#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/server.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

/// Which entity something aims at: a reference that crosses as the
/// receiver's name for the entity.
struct Aim {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("0e4d7b92-3a61-4f8c-b5d2-9c17e6a04f38");
    static constexpr std::string_view kComponentName = "scenario.aim";
    world::EntityHandle target;
    float range = 0;
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

ComponentCodec aimCodec() {
    return {.component = Aim::kComponentTypeId,
            .size = sizeof(Aim),
            .fields = {{offsetof(Aim, target), WireKind::Entity}, {offsetof(Aim, range), WireKind::F32}}};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Position>().add<Steer>().add<Aim>();
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

/// The player's side of Move, for the client to predict with: the same
/// arithmetic on the same values in the same order.
class MovePredictor final : public world_replication::Predictor {
public:
    std::span<const std::byte> get(schema::ComponentTypeId component) const noexcept override {
        if (component == Position::kComponentTypeId) {
            return std::as_bytes(std::span{&position_, 1});
        }
        if (component == Steer::kComponentTypeId) {
            return std::as_bytes(std::span{&steer_, 1});
        }
        return {};
    }
    void rate(world::TickRate) noexcept override {
    }
    result::Status place(std::span<const world_replication::NeighborValue>) override {
        return {};
    }
    result::Status set(schema::ComponentTypeId component, std::span<const std::byte> value) override {
        if (component == Position::kComponentTypeId && value.size() == sizeof position_) {
            std::memcpy(&position_, value.data(), sizeof position_);
        } else if (component == Steer::kComponentTypeId && value.size() == sizeof steer_) {
            std::memcpy(&steer_, value.data(), sizeof steer_);
        }
        return {};
    }
    result::Status step(std::span<const std::byte> input) override {
        std::memcpy(&steer_, input.data(), sizeof steer_);
        position_.x += steer_.dx;
        position_.y += steer_.dy;
        return {};
    }

private:
    Position position_;
    Steer steer_;
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

/// What differs between scenarios besides the network.
struct Options {
    std::size_t stateBytesPerTick = 1092;
    bool predicting = false;
    std::optional<world_replication::InterestSettings> interest;
    bool interpolating = false;
    /// How much time one step is.
    MonotonicDuration stepLength = MonotonicDuration::fromMilliseconds(16);
};

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

    MovePredictor predictor;
    MonotonicDuration stepLength;

    explicit Scenario(network_loopback::LoopbackConditions conditions, Options options = {})
        : network(clock, conditions), stepLength(options.stepLength) {
        serverSessions = *network::Sessions::server(
            *serverTransport, clock, {.profile = kSessions, .expected = compatibility(), .seed = 1});
        RAWFRAME_EXPECT(serverSessions->listen({"server"}).has_value());
        server = *world_replication::ReplicationServer::create(
            *serverSessions,
            {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
             .playerComponents = {Position::kComponentTypeId, Steer::kComponentTypeId},
             .input = steerCodec(),
             .interest = std::move(options.interest),
             .stateBytesPerTick = options.stateBytesPerTick});
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(server->declareSystems(*schema, declarations).has_value());
        move = std::make_unique<Move>(*schema);
        declarations.push_back(world::SystemDeclaration{.identity = "scenario.move", .system = move.get()});
        schedule.emplace(*world::Schedule::compile(declarations, *schema));

        clientSessions = *network::Sessions::client(*clientTransport, clock, {.profile = kSessions, .seed = 2});
        client = *world_replication::ReplicationClient::create(
            *clientSessions,
            clientWorld,
            {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
             .input = steerCodec(),
             .prediction = options.predicting ? std::optional{world_replication::PredictionSettings{
                                                    .predictor = &predictor,
                                                    .predicted = {Position::kComponentTypeId, Steer::kComponentTypeId}}}
                                              : std::nullopt,
             .interpolation = options.interpolating
                                  ? std::optional{world_replication::InterpolationSettings{
                                        .interpolated = {Position::kComponentTypeId}, .clock = &clock}}
                                  : std::nullopt});
        RAWFRAME_EXPECT(
            client
                ->connect(
                    {"server"},
                    network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
                .has_value());
    }

    /// One step: the server pumps and ticks, the client pumps and steers.
    void step(Steer steer) {
        clock.advance(stepLength);
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

    /// Time passes between server ticks, and the client shows what it has.
    void betweenTicks(MonotonicDuration elapsed) {
        clock.advance(elapsed);
        client->pump();
    }

    const Position* firstPlayerPosition() {
        const world::EntityHandle kPlayer = server->player(network::ConnectionId{1});
        return kPlayer.isNull() ? nullptr : serverWorld.get(kPlayer, *schema->key<Position>());
    }

    /// Where the client mirrors something other than its own player.
    std::vector<float> othersAt() {
        std::vector<float> found;
        auto query = world::Query<world::Read<Position>>::resolve(*schema);
        query->forEach(clientWorld, [&](world::EntityHandle entity, const Position& position) {
            if (entity != client->owned()) {
                found.push_back(position.x);
            }
        });
        std::sort(found.begin(), found.end());
        return found;
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

RAWFRAME_TEST(AnEntityNamedInAValueIsTheClientsMirrorOfIt) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kAim = *scenario.schema->key<Aim>();
    // A target, a turret aiming at it, and a turret aiming at something
    // that is never replicated.
    const world::EntityHandle kTarget = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTarget, kPosition, Position{42, 0}).has_value());
    const world::EntityHandle kHidden = *scenario.serverWorld.create();
    const world::EntityHandle kTurret = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kPosition, Position{1, 0}).has_value());
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kAim, Aim{.target = kTarget, .range = 5}).has_value());
    const world::EntityHandle kBlind = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kBlind, kPosition, Position{2, 0}).has_value());
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kBlind, kAim, Aim{.target = kHidden, .range = 6}).has_value());
    for (int step = 0; step < 60; ++step) {
        scenario.step(Steer{});
    }
    const auto kAimOf = [&](float range) {
        Aim found{.target = {}, .range = -1};
        auto query = world::Query<world::Read<Aim>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&](world::EntityHandle, const Aim& aim) {
            if (aim.range == range) {
                found = aim;
            }
        });
        return found;
    };
    // The turret aims at the client's entity standing for the target, not
    // at the server's handle.
    const Aim kSeen = kAimOf(5);
    RAWFRAME_EXPECT(kSeen.range == 5 && !kSeen.target.isNull());
    const Position* aimedAt = scenario.clientWorld.get(kSeen.target, kPosition);
    RAWFRAME_EXPECT(aimedAt != nullptr && aimedAt->x == 42);
    // What the client has never been told of is named as no entity.
    const Aim kBlindSeen = kAimOf(6);
    RAWFRAME_EXPECT(kBlindSeen.range == 6 && kBlindSeen.target.isNull());

    // Once the target is gone, the turret aims at nothing again.
    RAWFRAME_EXPECT(scenario.serverWorld.destroy(kTarget).has_value());
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(kAimOf(5).target.isNull());
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

RAWFRAME_TEST(ANarrowBudgetSendsThePlayerFirstAndTheRestInTurn) {
    // Room for the header and two or three records a tick: far less than
    // twenty moving props need.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}, {.stateBytesPerTick = 64}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kSteer = *scenario.schema->key<Steer>();
    for (int index = 0; index < 20; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index), 0}).has_value());
        RAWFRAME_EXPECT(scenario.serverWorld.insert(kProp, kSteer, Steer{0, 1}).has_value());
    }
    for (int step = 0; step < 120; ++step) {
        scenario.step(Steer{1, 0});
    }
    const auto kServer = scenario.server->statistics();
    RAWFRAME_EXPECT(kServer.recordsDeferred > 0);
    RAWFRAME_EXPECT(kServer.stateBytes <= kServer.stateDatagrams * 64);
    // The player's own position is never deferred: it is exactly what the
    // server committed at the newest tick the client heard of.
    const Position* mirror = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    const auto kCommitted = scenario.history.find(scenario.client->serverTick());
    RAWFRAME_EXPECT(mirror != nullptr && kCommitted != scenario.history.end() && kCommitted->second.x == mirror->x);
    // Every prop has been sent within the last few ticks, the longest waiting
    // first: none is further behind than the budget's turn around them.
    int fresh = 0;
    auto query = world::Query<world::Read<Position>>::resolve(*scenario.schema);
    query->forEach(scenario.clientWorld, [&fresh](world::EntityHandle, const Position& position) {
        fresh += position.y > 90 ? 1 : 0;
    });
    RAWFRAME_EXPECT(fresh == 20);
}

RAWFRAME_TEST(APredictingClientRunsAheadAndIsConfirmed) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(40)}, {.predicting = true}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int step = 0; step < 120; ++step) {
        scenario.step(Steer{static_cast<float>(step % 5), 0.25F});
    }
    const auto kStatistics = scenario.client->predictionStatistics();
    RAWFRAME_EXPECT(kStatistics.predictedTicks > 90 && kStatistics.confirmed > 60);
    // On a clean network only admission and the first pace adjustments
    // mispredict: the server held or went neutral on ticks the client had
    // not labelled yet.
    RAWFRAME_EXPECT(kStatistics.rollbacks <= 3);
    // The player is shown where its own input has taken it, ahead of the
    // server, which has not consumed the newest commands yet.
    const Position* shown = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    const Position* server = scenario.firstPlayerPosition();
    RAWFRAME_EXPECT(shown != nullptr && server != nullptr && shown->y > server->y);
    // Idle input: the server catches up and both agree exactly.
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    shown = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    server = scenario.firstPlayerPosition();
    RAWFRAME_EXPECT(shown != nullptr && server != nullptr && shown->x == server->x && shown->y == server->y);
}

RAWFRAME_TEST(PredictionRecoversFromLostInput) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(30),
                       .jitter = MonotonicDuration::fromMilliseconds(30),
                       .datagramLossPerMillion = 300'000,
                       .seed = 5},
                      {.predicting = true}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int step = 0; step < 150; ++step) {
        scenario.step(Steer{step % 7 == 0 ? -3.0F : 1.0F, static_cast<float>(step % 3)});
    }
    for (int step = 0; step < 40; ++step) {
        scenario.step(Steer{});
    }
    // Whatever was lost and held on the way, the client ends where the server is.
    const Position* shown = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    const Position* server = scenario.firstPlayerPosition();
    RAWFRAME_EXPECT(shown != nullptr && server != nullptr && shown->x == server->x && shown->y == server->y);
    RAWFRAME_EXPECT(scenario.client->predictionStatistics().confirmed > 0);
}

RAWFRAME_TEST(InterestSendsWhatIsNearThePlayerAndAlwaysThePlayer) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)},
                      {.interest = world_replication::InterestSettings{
                           .position = Position::kComponentTypeId,
                           .axes = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}},
                           .radius = 10,
                           .leaveRadius = 12}}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kSteer = *scenario.schema->key<Steer>();
    for (const float kX : {5.0F, 30.0F, 1000.0F}) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(scenario.serverWorld.insert(kProp, kPosition, Position{kX, 0}).has_value());
    }
    // Without a position, it is everywhere and sent to everyone.
    const world::EntityHandle kEverywhere = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kEverywhere, kSteer, Steer{2, 2}).has_value());
    const auto kPlaceAt = [&](float x) {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        if (!kPlayer.isNull()) {
            *scenario.serverWorld.get(kPlayer, kPosition) = Position{x, 0};
        }
        for (int step = 0; step < 20; ++step) {
            scenario.step(Steer{});
        }
    };
    const auto kEverywhereMirrored = [&] {
        std::size_t count = 0;
        auto query = world::Query<world::Read<Steer>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&](world::EntityHandle entity, const Steer& steer) {
            count += entity != scenario.client->owned() && steer.dx == 2 ? 1 : 0;
        });
        return count;
    };

    kPlaceAt(0);
    kPlaceAt(0);
    RAWFRAME_EXPECT(!scenario.client->owned().isNull());
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{5});
    RAWFRAME_EXPECT(kEverywhereMirrored() == 1);

    // Far from where it was: what it left behind is retired, what it came
    // to is declared.
    kPlaceAt(30);
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{30});
    RAWFRAME_EXPECT(scenario.server->statistics().interestLeft == 1);
    // Beyond the radius and within the leaving radius, it stays; beyond
    // that, it goes.
    kPlaceAt(41);
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{30});
    kPlaceAt(43);
    RAWFRAME_EXPECT(scenario.othersAt().empty());
    kPlaceAt(1000);
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{1000});
    // Coming back, it is declared afresh and its value sent again.
    kPlaceAt(0);
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{5});
    RAWFRAME_EXPECT(kEverywhereMirrored() == 1);
    // The player itself was never out of its own interest.
    const Position* mirror = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    RAWFRAME_EXPECT(mirror != nullptr && mirror->x == 0);
}

RAWFRAME_TEST(WhatIsAcknowledgedIsForgottenAsPlayGoesOn) {
    // A state datagram an acknowledgement can no longer reach is forgotten
    // once a newer acknowledgement arrives (D217), so what the server holds
    // for a connection that keeps acknowledging stays flat.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 100; ++step) {
        scenario.step(Steer{.dx = 1, .dy = 0});
    }
    const std::size_t kEarly = scenario.server->heldBytes();
    for (int step = 0; step < 300; ++step) {
        scenario.step(Steer{.dx = 1, .dy = 0});
    }
    RAWFRAME_EXPECT(!scenario.client->owned().isNull());
    RAWFRAME_EXPECT(scenario.server->heldBytes() <= kEarly + 256);
}

RAWFRAME_TEST(InterestCellsMissNothingWithinReach) {
    // A field of props a meter and a quarter apart and a player put down
    // on cell edges and between them (cells are 10.1 wide): the client
    // mirrors exactly what is within the radius, as though every prop were
    // measured (D209).
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)},
                      {.stateBytesPerTick = 8192,
                       .interest = world_replication::InterestSettings{
                           .position = Position::kComponentTypeId,
                           .axes = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}},
                           .radius = 10,
                           .leaveRadius = 10}}};
    const auto kPosition = *scenario.schema->key<Position>();
    std::vector<Position> props;
    for (int x = -24; x <= 24; ++x) {
        for (int y = -24; y <= 24; ++y) {
            props.push_back(Position{static_cast<float>(x) * 1.25F, static_cast<float>(y) * 1.25F});
        }
    }
    // Far out, and not a place at all: never within reach.
    props.push_back(Position{1e30F, 0});
    props.push_back(Position{std::numeric_limits<float>::infinity(), 0});
    props.push_back(Position{std::numeric_limits<float>::quiet_NaN(), 0});
    for (const Position& prop : props) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(scenario.serverWorld.insert(kProp, kPosition, prop).has_value());
    }
    const auto kMirrored = [&] {
        std::vector<std::pair<float, float>> found;
        auto query = world::Query<world::Read<Position>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&](world::EntityHandle entity, const Position& position) {
            if (entity != scenario.client->owned()) {
                found.emplace_back(position.x, position.y);
            }
        });
        std::ranges::sort(found);
        return found;
    };
    const auto kWithin = [&](float x, float y) {
        std::vector<std::pair<float, float>> found;
        for (const Position& prop : props) {
            const double kX = static_cast<double>(prop.x) - x;
            const double kY = static_cast<double>(prop.y) - y;
            if ((kX * kX) + (kY * kY) <= 100) {
                found.emplace_back(prop.x, prop.y);
            }
        }
        std::ranges::sort(found);
        return found;
    };
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{});
    }
    for (const auto& [kX, kY] : std::vector<std::pair<float, float>>{
             {0, 0}, {10.1F, 0}, {-10.1F, 10.1F}, {3.3F, -7.7F}, {20.2F, 20.2F}, {1e30F, 0}}) {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        RAWFRAME_EXPECT(!kPlayer.isNull());
        if (kPlayer.isNull()) {
            return;
        }
        *scenario.serverWorld.get(kPlayer, kPosition) = Position{kX, kY};
        for (int step = 0; step < 30; ++step) {
            scenario.step(Steer{});
        }
        const auto kExpected = kWithin(kX, kY);
        RAWFRAME_EXPECT(kMirrored() == kExpected);
        RAWFRAME_EXPECT(kX > 1e29F ? kExpected.size() == 1 : kExpected.size() > 100);
    }
}

RAWFRAME_TEST(RemoteEntitiesAreShownBetweenStates) {
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(30),
                       .jitter = MonotonicDuration::fromMilliseconds(20),
                       .datagramLossPerMillion = 100'000,
                       .seed = 11},
                      {.interpolating = true, .stepLength = MonotonicDuration{8'333'333}}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kSteer = *scenario.schema->key<Steer>();
    // Props moving steadily, each known by its y.
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 5; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(props.back(), kPosition, Position{0, static_cast<float>(index)}).has_value());
        RAWFRAME_EXPECT(scenario.serverWorld.insert(props.back(), kSteer, Steer{0.5F, 0}).has_value());
    }
    // Where each prop truly was after each server tick.
    std::map<std::uint64_t, std::vector<float>> truth;
    const auto kStep = [&] {
        const std::uint64_t kRan = scenario.tick.value;
        scenario.step(Steer{});
        std::vector<float>& at = truth[kRan];
        for (const world::EntityHandle kProp : props) {
            at.push_back(scenario.serverWorld.get(kProp, kPosition)->x);
        }
    };
    for (int step = 0; step < 90; ++step) {
        kStep();
        scenario.betweenTicks(MonotonicDuration{8'333'334});
    }
    // Shown exactly where each prop was at the moment shown, which the
    // client shows at twice the tick rate, so half the time between ticks,
    // though states arrive late, jittered, and not at all.
    double worst = 0;
    int compared = 0;
    for (int step = 0; step < 120; ++step) {
        if (step % 2 == 0) {
            kStep();
        } else {
            scenario.betweenTicks(MonotonicDuration{8'333'334});
        }
        const std::optional<double> kPerceived = scenario.client->perceivedTick();
        RAWFRAME_EXPECT(kPerceived.has_value());
        if (!kPerceived.has_value()) {
            return;
        }
        const auto kBase = static_cast<std::uint64_t>(*kPerceived);
        const double kFraction = *kPerceived - static_cast<double>(kBase);
        const auto kBefore = truth.find(kBase);
        const auto kAfter = truth.find(kBase + 1);
        RAWFRAME_EXPECT(kBefore != truth.end() && kAfter != truth.end());
        if (kBefore == truth.end() || kAfter == truth.end()) {
            return;
        }
        auto query = world::Query<world::Read<Position>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&](world::EntityHandle entity, const Position& shown) {
            if (entity == scenario.client->owned()) {
                return;
            }
            const auto kIndex = static_cast<std::size_t>(shown.y);
            const double kTrue =
                kBefore->second[kIndex] + ((kAfter->second[kIndex] - kBefore->second[kIndex]) * kFraction);
            worst = std::max(worst, std::abs(shown.x - kTrue));
            ++compared;
        });
    }
    RAWFRAME_EXPECT(compared == 600);
    RAWFRAME_EXPECT(worst < 0.001);
    // The moment shown is the delay behind the newest state, give or take
    // the network's jitter.
    const double kBehind = static_cast<double>(scenario.client->serverTick()) - *scenario.client->perceivedTick();
    RAWFRAME_EXPECT(kBehind > 4 && kBehind < 9);
    RAWFRAME_EXPECT(scenario.client->interpolationStatistics().blended > 500);
}
