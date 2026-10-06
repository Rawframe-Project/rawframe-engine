#pragma once

// The replication scenarios' world (replication_test.cpp,
// replication_commands_test.cpp): a server World and a client World over
// loopback, the client's player steering a position the server moves, and
// what each scenario varies besides the network.

#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/records.h"
#include "rawframe/world_replication/server.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace rawframe::scenario {

using execution::ManualClock;
using execution::MonotonicDuration;
using world_replication::ComponentCodec;
using world_replication::WireKind;

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

inline ComponentCodec positionCodec() {
    return {.component = Position::kComponentTypeId,
            .size = sizeof(Position),
            .fields = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}}};
}

inline ComponentCodec steerCodec() {
    return {.component = Steer::kComponentTypeId,
            .size = sizeof(Steer),
            .fields = {{offsetof(Steer, dx), WireKind::F32}, {offsetof(Steer, dy), WireKind::F32}}};
}

inline ComponentCodec aimCodec() {
    return {.component = Aim::kComponentTypeId,
            .size = sizeof(Aim),
            .fields = {{offsetof(Aim, target), WireKind::Entity}, {offsetof(Aim, range), WireKind::F32}}};
}

inline std::shared_ptr<const schema::SchemaRegistry> registry() {
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

inline network::Compatibility compatibility() {
    network::Compatibility made{.protocol = network::protocolFingerprint()};
    made.game.bytes.fill(std::byte{7});
    return made;
}

/// What differs between scenarios besides the network.
struct Options {
    std::uint32_t statePeriod = 1;
    std::size_t stateBytesPerPublish = 1092;
    bool predicting = false;
    std::optional<world_replication::InterestSettings> interest;
    bool interpolating = false;
    /// How much time one step is.
    MonotonicDuration stepLength = MonotonicDuration::fromMilliseconds(16);
    /// The game's commands' sizes; the command lane is declared with any.
    std::vector<std::size_t> commandSizes;
};

inline std::vector<network::EventLaneDeclaration> lanesFor(const Options& options) {
    std::vector<network::EventLaneDeclaration> lanes = {world_replication::engineLane()};
    if (!options.commandSizes.empty()) {
        lanes.push_back(world_replication::gameCommandLane(*std::ranges::max_element(options.commandSizes)));
    }
    return lanes;
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

    MovePredictor predictor;
    MonotonicDuration stepLength;

    explicit Scenario(network_loopback::LoopbackConditions conditions, Options options = {})
        : network(clock, conditions), stepLength(options.stepLength) {
        serverSessions = *network::Sessions::server(
            *serverTransport,
            clock,
            {.profile = kSessions, .expected = compatibility(), .lanes = lanesFor(options), .seed = 1});
        RAWFRAME_EXPECT(serverSessions->listen({"server"}).has_value());
        server = *world_replication::ReplicationServer::create(
            *serverSessions,
            {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
             .playerComponents = {Position::kComponentTypeId, Steer::kComponentTypeId},
             .input = steerCodec(),
             .interest = std::move(options.interest),
             .statePeriod = options.statePeriod,
             .stateBytesPerPublish = options.stateBytesPerPublish,
             .commandSizes = options.commandSizes});
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(server->declareSystems(*schema, declarations).has_value());
        move = std::make_unique<Move>(*schema);
        declarations.push_back(world::SystemDeclaration{.identity = "scenario.move", .system = move.get()});
        schedule.emplace(*world::Schedule::compile(declarations, *schema));

        clientSessions = *network::Sessions::client(
            *clientTransport, clock, {.profile = kSessions, .lanes = lanesFor(options), .seed = 2});
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

} // namespace rawframe::scenario
