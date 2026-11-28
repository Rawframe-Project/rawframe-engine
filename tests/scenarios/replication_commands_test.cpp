// A player's commands from the client World to the server's over
// loopback: taken with the player and tick, bounded a tick, struck when
// misshapen, and dropped with a player gone before they are taken. And the
// game's messages the other way, to each connection their audience holds.

#include "replication_scenario.h"

using namespace rawframe;
using namespace rawframe::scenario;

RAWFRAME_TEST(APlayersCommandsReachTheServerCheckedAndBounded) {
    // A command of a kind the game declares, at its exact size, is
    // taken with its player and the tick it arrived before; past the
    // commands one connection may have waiting each is dropped unread; one
    // of another kind or size strikes the connection; one whose player is
    // gone by the take is no one's.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}, {.commandSizes = {4, 8}}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
    RAWFRAME_EXPECT(scenario.client->admitted() && !kPlayer.isNull());
    const auto kSend = [&](std::uint32_t kind, std::size_t size, std::byte fill) {
        return scenario.client->sendCommand({.kind = kind, .value = std::vector<std::byte>(size, fill)}).has_value();
    };
    RAWFRAME_EXPECT(kSend(0, 4, std::byte{1}) && kSend(1, 8, std::byte{2}) && kSend(0, 4, std::byte{3}));
    for (int step = 0; step < 5; ++step) {
        scenario.step(Steer{});
    }
    std::vector<world_replication::ReceivedCommand> taken;
    scenario.server->takeCommands(taken);
    RAWFRAME_EXPECT(taken.size() == 3 && scenario.client->statistics().commandsSent == 3);
    if (taken.size() == 3) {
        RAWFRAME_EXPECT(taken[0].player == kPlayer && taken[0].kind == 0 && taken[0].value[0] == std::byte{1});
        RAWFRAME_EXPECT(taken[1].kind == 1 && taken[1].value.size() == 8 && taken[2].value[0] == std::byte{3});
        RAWFRAME_EXPECT(taken[0].tick.value > 30 && taken[0].tick.value <= scenario.tick.value);
    }
    // Taken once.
    taken.clear();
    scenario.server->takeCommands(taken);
    RAWFRAME_EXPECT(taken.empty());
    // Forty for one tick: thirty-two are taken, however often the host
    // takes them, and eight dropped.
    for (int command = 0; command < 40; ++command) {
        RAWFRAME_EXPECT(kSend(0, 4, std::byte{4}));
    }
    for (int pump = 0; pump < 20; ++pump) {
        scenario.clock.advance(MonotonicDuration::fromMilliseconds(4));
        scenario.server->pump(scenario.serverWorld, scenario.tick);
        scenario.server->takeCommands(taken);
    }
    RAWFRAME_EXPECT(taken.size() == 32 && scenario.server->statistics().commandsLimited == 8 &&
                    scenario.server->statistics().commandsTaken == 35);
    // A kind the game does not declare, and a declared one at another size.
    RAWFRAME_EXPECT(kSend(2, 4, std::byte{5}) && kSend(1, 4, std::byte{6}));
    for (int step = 0; step < 5; ++step) {
        scenario.step(Steer{});
    }
    taken.clear();
    scenario.server->takeCommands(taken);
    RAWFRAME_EXPECT(taken.empty() && scenario.server->statistics().strikes == 2 &&
                    scenario.firstPlayerPosition() != nullptr);
    // A player gone before the game takes its commands asked for nothing.
    RAWFRAME_EXPECT(kSend(0, 4, std::byte{7}));
    for (int step = 0; step < 5; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.server->terminate(scenario.serverWorld, kPlayer, {}));
    scenario.server->takeCommands(taken);
    RAWFRAME_EXPECT(taken.empty() && scenario.server->statistics().commandsTaken == 36);
}

RAWFRAME_TEST(MisshapenCommandsStrikeAPlayerOutOfTheWorld) {
    // The eighth misshapen command closes the connection
    // and takes its player out of the World, as misshapen input does.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}, {.commandSizes = {4}}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
    RAWFRAME_EXPECT(scenario.client->admitted() && !kPlayer.isNull());
    for (std::uint64_t strike = 0; strike < network::kMaximumStrikes; ++strike) {
        static_cast<void>(scenario.client->sendCommand({.kind = 0, .value = std::vector<std::byte>(2)}));
        for (int step = 0; step < 3; ++step) {
            scenario.clock.advance(scenario.stepLength);
            scenario.server->pump(scenario.serverWorld, scenario.tick);
            RAWFRAME_EXPECT(
                scenario.schedule->runTick(scenario.serverWorld, scenario.tick, *world::TickRate::of(60)).has_value());
            scenario.client->pump();
        }
    }
    RAWFRAME_EXPECT(scenario.server->statistics().strikes == network::kMaximumStrikes);
    RAWFRAME_EXPECT(!scenario.serverWorld.alive(kPlayer) && scenario.server->connections() == 0);
}

namespace {

/// The values of the four-byte game messages a client heard, in order.
struct Heard final : world_replication::MessageSink {
    std::vector<std::uint32_t> values;

    void deliver(world_replication::ReceivedMessage message) noexcept override {
        std::uint32_t value = 0;
        if (message.value.size() == sizeof value) {
            std::memcpy(&value, message.value.data(), sizeof value);
        }
        values.push_back(value);
    }
};

} // namespace

RAWFRAME_TEST(AMessageReachesEachConnectionItsAudienceHoldsOnce) {
    // Two players far apart, each with an entity near it: a message to
    // everyone reaches both, one to who sees an entity reaches only the
    // player whose interest holds it (and an entity's own player), and one
    // to a component's holders reaches only the player holding it.
    Heard first;
    Heard second;
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)},
                      {.interest = world_replication::InterestSettings{.position = Position::kComponentTypeId,
                                                                       .axes = {{offsetof(Position, x), WireKind::F32},
                                                                                {offsetof(Position, y), WireKind::F32}},
                                                                       .radius = 10,
                                                                       .leaveRadius = 12},
                       .messages = &first}};
    world::World secondWorld{scenario.schema};
    auto transport = *scenario.network.provider(kTransport);
    auto sessions = *network::Sessions::client(
        *transport, scenario.clock, {.profile = kSessions, .lanes = lanesFor({.messages = &second}), .seed = 3});
    auto client = *world_replication::ReplicationClient::create(
        *sessions,
        secondWorld,
        {.table = {.components = {positionCodec(), steerCodec(), aimCodec()}},
         .input = steerCodec(),
         .messages = &second});
    RAWFRAME_EXPECT(
        client
            ->connect({"server"},
                      network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
            .has_value());
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kSteps = [&](int count) {
        for (int step = 0; step < count; ++step) {
            scenario.step(Steer{});
            client->pump();
            if (client->admitted()) {
                Steer still{};
                RAWFRAME_EXPECT(client->submitInput(std::as_bytes(std::span{&still, 1})).has_value());
            }
        }
    };
    kSteps(20);
    const world::EntityHandle kOne = scenario.server->player(network::ConnectionId{1});
    const world::EntityHandle kTwo = scenario.server->player(network::ConnectionId{2});
    RAWFRAME_EXPECT(!kOne.isNull() && !kTwo.isNull() && kOne != kTwo);
    if (kOne.isNull() || kTwo.isNull()) {
        return;
    }
    *scenario.serverWorld.get(kOne, kPosition) = Position{0, 0};
    *scenario.serverWorld.get(kTwo, kPosition) = Position{100, 0};
    const world::EntityHandle kNearOne = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kNearOne, kPosition, Position{5, 0}).has_value());
    RAWFRAME_EXPECT(
        scenario.serverWorld.insert(kTwo, *scenario.schema->key<Aim>(), Aim{.target = {}, .range = 1}).has_value());
    kSteps(30);
    const auto kPost = [&](std::uint32_t value,
                           world_replication::Audience audience,
                           world::EntityHandle entity = {},
                           schema::ComponentTypeId holding = {}) {
        world_replication::PostedMessage message{.player = entity,
                                                 .kind = 0,
                                                 .value = std::vector<std::byte>(sizeof value),
                                                 .audience = audience,
                                                 .holding = holding};
        std::memcpy(message.value.data(), &value, sizeof value);
        scenario.server->post(scenario.serverWorld, message);
    };
    using world_replication::Audience;
    kPost(1, Audience::Everyone);
    kPost(2, Audience::Seeing, kNearOne);
    kPost(3, Audience::Seeing, kTwo);
    kPost(4, Audience::Holding, {}, Aim::kComponentTypeId);
    kPost(5, Audience::Player, kOne);
    kSteps(10);
    RAWFRAME_EXPECT((first.values == std::vector<std::uint32_t>{1, 2, 5}));
    RAWFRAME_EXPECT((second.values == std::vector<std::uint32_t>{1, 3, 4}));
    RAWFRAME_EXPECT(scenario.server->statistics().messagesSent == 6 &&
                    scenario.server->statistics().messagesUndelivered == 0);
}
