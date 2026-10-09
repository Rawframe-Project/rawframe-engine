// A server World and a client World over loopback: the client is admitted,
// gets a player, mirrors every replicated entity, drives its player with
// input, and sees exactly the server's committed values, late but never wrong,
// through loss and reordering, and only what is in its interest.

#include "replication_scenario.h"

using namespace rawframe;
using namespace rawframe::scenario;

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

RAWFRAME_TEST(AConnectionAdmittedAfterTheStoppingNoticeIsToldToo) {
    // SPEC-0012's stopping notice, asked for each iteration a server drains
    // (D585): a connection a tick admits after the first asking hears it
    // from the next, and play goes on.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    scenario.server->noticeStopping();
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{1, 0});
    }
    RAWFRAME_EXPECT(scenario.client->admitted() && !scenario.client->serverStopping());
    scenario.server->noticeStopping();
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{1, 0});
    }
    RAWFRAME_EXPECT(scenario.client->serverStopping() && scenario.client->admitted() &&
                    scenario.server->connections() == 1);
}

RAWFRAME_TEST(ATerminatedPlayerIsToldWhyAndLeaves) {
    // ADR-0073 (D267): the reason arrives on the engine's lane, the player
    // leaves the server's World at once, and the client ends its session.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{1, 0});
    }
    const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
    RAWFRAME_EXPECT(scenario.client->admitted() && !kPlayer.isNull() && scenario.mirrored() == 1);
    RAWFRAME_EXPECT(scenario.server->terminate(
        scenario.serverWorld, kPlayer, {.reason = network::TerminationReason::Game, .note = "out of bounds"}));
    RAWFRAME_EXPECT(!scenario.serverWorld.alive(kPlayer) && scenario.server->connections() == 0 &&
                    scenario.server->statistics().terminated == 1);
    // Ended once: a second asks for no one.
    RAWFRAME_EXPECT(!scenario.server->terminate(scenario.serverWorld, kPlayer, {}));
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{});
    }
    const auto& kTermination = scenario.client->termination();
    RAWFRAME_EXPECT(kTermination.has_value() && kTermination->reason == network::TerminationReason::Game &&
                    kTermination->note == "out of bounds");
    RAWFRAME_EXPECT(scenario.client->ended() && !scenario.client->admitted() && scenario.mirrored() == 0);
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
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}, {.stateBytesPerPublish = 64}};
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
    // not labelled yet. Each such tick is told as it happens, even while
    // the player stands still there (D249), so a few, not one.
    RAWFRAME_EXPECT(kStatistics.rollbacks <= 6);
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

RAWFRAME_TEST(AnIdlePlayerIsConfirmedAndMovesAtOnce) {
    // A player standing still changes nothing the server would send, yet
    // its predictions are confirmed all along, so none waits for a full
    // window when it starts to move (D249).
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(40)}, {.predicting = true}};
    for (int step = 0; step < 200; ++step) {
        scenario.step(Steer{});
    }
    const auto kIdle = scenario.client->predictionStatistics();
    RAWFRAME_EXPECT(kIdle.stalled == 0 && kIdle.confirmed > 150);
    for (int step = 0; step < 10; ++step) {
        scenario.step(Steer{1, 0});
    }
    const auto kMoving = scenario.client->predictionStatistics();
    RAWFRAME_EXPECT(kMoving.stalled == 0 && kMoving.predictedTicks >= kIdle.predictedTicks + 10);
}

RAWFRAME_TEST(StateGoesOutOnceAPeriodAndPredictionHolds) {
    // SPEC-0013's 20 Hz current-state production at 60 ticks a second
    // (D222): a state datagram every third tick, and a predicting client
    // confirmed as often and agreeing exactly once input is idle.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(40)}, {.statePeriod = 3, .predicting = true}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int step = 0; step < 180; ++step) {
        scenario.step(Steer{static_cast<float>(step % 5), 0.25F});
    }
    const std::uint64_t kDatagrams = scenario.server->statistics().stateDatagrams;
    RAWFRAME_EXPECT(kDatagrams >= 50 && kDatagrams <= 60);
    const auto kStatistics = scenario.client->predictionStatistics();
    RAWFRAME_EXPECT(kStatistics.predictedTicks > 150 && kStatistics.confirmed > 40 && kStatistics.rollbacks <= 3);
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    const Position* shown = scenario.clientWorld.get(scenario.client->owned(), kPosition);
    const Position* server = scenario.firstPlayerPosition();
    RAWFRAME_EXPECT(shown != nullptr && server != nullptr && shown->x == server->x && shown->y == server->y);
}

RAWFRAME_TEST(AClientSendingMalformedInputStrikesOut) {
    // SPEC-0013's strikes (D223): malformed input windows are refused, and
    // the eighth within ten seconds closes the connection and takes its
    // player out of the World.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.client->admitted() && scenario.firstPlayerPosition() != nullptr);
    const std::uint64_t kEpoch = scenario.client->accept().has_value() ? scenario.client->accept()->inputEpoch : 0;
    const std::vector<std::byte> kGarbage(3, std::byte{0xff});
    const auto kStrike = [&](std::uint64_t sequence) {
        static_cast<void>(scenario.clientSessions->sendDatagram(network::ConnectionId{1},
                                                                {.lane = network::DatagramLane::Input,
                                                                 .laneEpoch = kEpoch,
                                                                 .sequence = sequence,
                                                                 .payloadType = world_replication::kInputWindowPayload,
                                                                 .payload = kGarbage}));
        // Ticks without input: once struck out, the client may still think
        // itself admitted until it hears the close.
        for (int step = 0; step < 5; ++step) {
            scenario.clock.advance(scenario.stepLength);
            scenario.server->pump(scenario.serverWorld, scenario.tick);
            RAWFRAME_EXPECT(
                scenario.schedule->runTick(scenario.serverWorld, scenario.tick, *world::TickRate::of(60)).has_value());
            scenario.client->pump();
        }
    };
    for (std::uint64_t strike = 1; strike < network::kMaximumStrikes; ++strike) {
        kStrike(1000 + strike);
    }
    RAWFRAME_EXPECT(scenario.firstPlayerPosition() != nullptr);
    kStrike(2000);
    RAWFRAME_EXPECT(scenario.firstPlayerPosition() == nullptr);
    RAWFRAME_EXPECT(scenario.server->statistics().strikes == network::kMaximumStrikes);
    RAWFRAME_EXPECT(scenario.serverSessions->struckOut() == 1);
}

RAWFRAME_TEST(AFloodOfInputIsDroppedUnreadPastTheCeilings) {
    // SPEC-0013's input ceilings (D225): at most one input window a tick,
    // and 64 KiB, are looked at in a second; what passes them is dropped
    // unread. The floods here are of an earlier epoch, late rather than
    // malformed, so nothing strikes the connection out.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.client->admitted());
    const std::uint64_t kLate = scenario.client->accept().has_value() ? scenario.client->accept()->inputEpoch + 1 : 0;
    std::uint64_t sequence = 10'000;
    const auto kFlood = [&](int perStep, std::uint64_t payloadType, std::size_t size) {
        const std::vector<std::byte> kPayload(size, std::byte{0xff});
        const std::uint64_t kBefore = scenario.server->statistics().inputsLimited;
        for (int step = 0; step < 60; ++step) {
            for (int flood = 0; flood < perStep; ++flood) {
                static_cast<void>(scenario.clientSessions->sendDatagram(network::ConnectionId{1},
                                                                        {.lane = network::DatagramLane::Input,
                                                                         .laneEpoch = kLate,
                                                                         .sequence = ++sequence,
                                                                         .payloadType = payloadType,
                                                                         .payload = kPayload}));
            }
            scenario.step(Steer{.dx = 1, .dy = 0});
        }
        return scenario.server->statistics().inputsLimited - kBefore;
    };
    // Ten windows a tick, 600 in a second of ticks: at most two seconds'
    // allowance, 120, is looked at.
    RAWFRAME_EXPECT(kFlood(10, world_replication::kInputWindowPayload, 8) >= 480);
    // Twenty kilobyte records a tick: at most two seconds' 64 KiB are.
    RAWFRAME_EXPECT(kFlood(20, 99, 1000) >= 1000);
    RAWFRAME_EXPECT(scenario.server->statistics().strikes == 0 && scenario.firstPlayerPosition() != nullptr);
}

RAWFRAME_TEST(InputThatWaitedTooLongIsDroppedNotPlayedLate) {
    // SPEC-0013's replaceable input age (D232): commands sent 15 to 30 ticks
    // ahead wait longer than 100 ms for their ticks, so each is dropped when
    // its tick comes and the tick plays as if it had not arrived. Paced
    // input, two ticks ahead, is never stale.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.client->admitted() && scenario.server->statistics().inputsStale == 0);
    const auto* kBefore = scenario.firstPlayerPosition();
    const float kX = kBefore != nullptr ? kBefore->x : 0;
    // Sixteen commands pushing right, for ticks well ahead of consumption.
    const Steer kRight{.dx = 5, .dy = 0};
    std::vector<std::byte> wire(8);
    network::Writer steer{wire};
    RAWFRAME_EXPECT(steerCodec().encode(reinterpret_cast<const std::byte*>(&kRight), steer).has_value());
    world_replication::InputWindow window{.newestInputTick = scenario.tick.value + 30};
    for (std::size_t command = 0; command < world_replication::kMaximumInputWindow; ++command) {
        window.commands.push_back(wire);
    }
    std::vector<std::byte> payload(512);
    network::Writer writer{payload};
    RAWFRAME_EXPECT(world_replication::encodeInputWindow(writer, window).has_value());
    RAWFRAME_EXPECT(scenario.clientSessions
                        ->sendDatagram(network::ConnectionId{1},
                                       {.lane = network::DatagramLane::Input,
                                        .laneEpoch = scenario.client->accept()->inputEpoch,
                                        .sequence = 1'000'000,
                                        .payloadType = world_replication::kInputWindowPayload,
                                        .payload = writer.written()})
                        .has_value());
    for (int step = 0; step < 40; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.server->statistics().inputsStale == world_replication::kMaximumInputWindow);
    const auto* kAfter = scenario.firstPlayerPosition();
    RAWFRAME_EXPECT(kAfter != nullptr && kAfter->x == kX);
}

RAWFRAME_TEST(InputPacedFromBurstsAndStallsComesBackOnTime) {
    // A browser page's client (D317): it samples four ticks' input at once
    // each frame, and once it stalls a third of a second and then samples
    // what it owes at once. The server paces by the least lead since its
    // last signal, so bursts arrive on time. It leaves out what it measured
    // while the client was silent past a command's age, and the client
    // jumps only for a lateness two signals in a row find, the second no
    // less: one making up a stall is not paced ahead for it, and is on time
    // once it has, not early (D323).
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.client->admitted());
    std::uint64_t owed = 0;
    // The most samples a frame takes: a page makes up a long stall over a
    // few frames.
    std::uint64_t most = 1000;
    const auto kTick = [&](bool sampling) {
        scenario.clock.advance(scenario.stepLength);
        scenario.server->pump(scenario.serverWorld, scenario.tick);
        RAWFRAME_EXPECT(
            scenario.schedule->runTick(scenario.serverWorld, scenario.tick, *world::TickRate::of(60)).has_value());
        scenario.client->pump();
        ++owed;
        if (sampling) {
            const Steer kSteer{.dx = 1, .dy = 0};
            for (std::uint64_t taken = 0; owed > 0 && taken < most; --owed, ++taken) {
                RAWFRAME_EXPECT(scenario.client->submitInput(std::as_bytes(std::span{&kSteer, 1})).has_value());
            }
        }
    };
    const auto kPlay = [&](int ticks, int every) {
        for (int at = 1; at <= ticks; ++at) {
            kTick(at % every == 0);
        }
    };
    const auto kMissed = [&] {
        const auto kServer = scenario.server->statistics();
        return kServer.inputsHeld + kServer.inputsNeutral + kServer.inputsStale;
    };
    // Bursts of four: settled after a second, none missed after that.
    kPlay(60, 4);
    const std::uint64_t kBurstsSettled = kMissed();
    kPlay(120, 4);
    RAWFRAME_EXPECT(kMissed() == kBurstsSettled);
    // A stall of twenty ticks, then bursts again: what it owed came too
    // late and is missed, and within half a second nothing more is; no
    // command waits past its age, and no sample is left unlabelled for it.
    const std::uint64_t kStaleBefore = scenario.server->statistics().inputsStale;
    const std::uint64_t kHeldBefore = scenario.client->statistics().samplesHeldBack;
    kPlay(20, 1000);
    kTick(true);
    kPlay(28, 4);
    const std::uint64_t kStallSettled = kMissed();
    kPlay(240, 4);
    RAWFRAME_EXPECT(kMissed() == kStallSettled);
    RAWFRAME_EXPECT(scenario.server->statistics().inputsStale == kStaleBefore &&
                    scenario.client->statistics().samplesHeldBack == kHeldBefore);
    // Stalled again, making up what it owed six samples a frame, two more
    // than a frame's ticks: late until it has, within two seconds, and
    // never early.
    most = 6;
    kPlay(20, 1000);
    kPlay(120, 4);
    const std::uint64_t kSpreadSettled = kMissed();
    kPlay(240, 4);
    RAWFRAME_EXPECT(kMissed() == kSpreadSettled);
    RAWFRAME_EXPECT(scenario.server->statistics().inputsStale == kStaleBefore);
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
                      {.stateBytesPerPublish = 8192,
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

RAWFRAME_TEST(AnAttemptEndedUnansweredIsAskedAgainFromNothing) {
    // D541: a client whose hello the server did not hear in its admission
    // time ends unanswered, no refusal; asked again, it is not ended, and
    // is admitted.
    Scenario scenario{{.latency = MonotonicDuration::fromMilliseconds(20)}};
    scenario.clock.advance(MonotonicDuration::fromSeconds(3));
    scenario.client->pump();
    RAWFRAME_EXPECT(scenario.client->ended() && !scenario.client->admitted() &&
                    !scenario.client->rejection().has_value() && !scenario.client->termination().has_value());
    RAWFRAME_EXPECT(
        scenario.client
            ->connect({"server"},
                      network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
            .has_value());
    RAWFRAME_EXPECT(!scenario.client->ended());
    for (int step = 0; step < 30; ++step) {
        scenario.step(Steer{1, 0});
    }
    RAWFRAME_EXPECT(scenario.client->admitted() && scenario.mirrored() == 1);
}
