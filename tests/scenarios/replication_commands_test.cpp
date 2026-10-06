// A player's commands (D425) from the client World to the server's over
// loopback: taken with the player and tick, bounded a tick, struck when
// misshapen, and dropped with a player gone before they are taken.

#include "replication_scenario.h"

using namespace rawframe;
using namespace rawframe::scenario;

RAWFRAME_TEST(APlayersCommandsReachTheServerCheckedAndBounded) {
    // D425: a command of a kind the game declares, at its exact size, is
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
    // D425 with D223: the eighth misshapen command closes the connection
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
