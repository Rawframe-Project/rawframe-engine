// The mappings a connection holds, over loopback: a join in view of many
// entities is declared over several publishes within the count a publish
// declares, and a mass removal retired within the mapping budget; while a
// retirement waits, its entity is sent nothing, named by nothing, and
// declared anew only once it went.
//
// What was declared together retires as one run of IDs, a few bytes
// whatever its length, so a removal that reaches the budget here is
// scattered: every other prop, each its own run.

#include "replication_scenario.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace rawframe;
using namespace rawframe::scenario;

namespace {

/// Interest within 10, held to 12.
world_replication::InterestSettings nearby() {
    return world_replication::InterestSettings{
        .position = Position::kComponentTypeId,
        .axes = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}},
        .radius = 10,
        .leaveRadius = 12};
}

/// Room for one retirement frame of about two dozen scattered runs.
constexpr std::size_t kBudget = 64;

} // namespace

RAWFRAME_TEST(AJoinDeclaresItsEntitiesWithinTheCountAPublishDeclares) {
    // A hundred props in view and a dozen declarations a publish: the join
    // declares them over several publishes, none of which tells the client
    // to make more than a dozen, and the client ends up mirroring every one.
    Scenario scenario{{}, {.mappingsPerPublish = 12}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int index = 0; index < 100; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index), 0}).has_value());
    }
    int declaring = 0;
    for (int step = 0; step < 60; ++step) {
        const std::size_t kBefore = scenario.clientWorld.entityCount();
        scenario.step(Steer{});
        const std::size_t kMade = scenario.clientWorld.entityCount() - kBefore;
        RAWFRAME_EXPECT(kMade <= 12);
        declaring += kMade > 0 ? 1 : 0;
    }
    RAWFRAME_EXPECT(declaring >= 8);
    RAWFRAME_EXPECT(scenario.mirrored() == 101);
    RAWFRAME_EXPECT(scenario.othersAt().size() == 100);
}

RAWFRAME_TEST(AMassRemovalRetiresItsMappingsWithinTheMappingBudget) {
    // Two hundred props mirrored, then every other one destroyed in one
    // tick: their retirements go over several publishes, none of which
    // sends the connection more mapping bytes than the budget, and the
    // client ends up holding its player, the hundred left and a turret.
    // The turret aims at the last prop destroyed, whose retirement waits
    // longest: from the removal on, the client sees it aim at nothing, not
    // at the prop it still holds.
    Scenario scenario{{}, {.mappingBytesPerPublish = kBudget}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kAim = *scenario.schema->key<Aim>();
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 200; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(props.back(), kPosition, Position{static_cast<float>(index), 0}).has_value());
    }
    const world::EntityHandle kTurret = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kPosition, Position{-1, 0}).has_value());
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kAim, Aim{.target = props[198], .range = 5}).has_value());
    const auto kAimed = [&] {
        world::EntityHandle target;
        auto query = world::Query<world::Read<Aim>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&target](world::EntityHandle, const Aim& aim) {
            target = aim.target;
        });
        return target;
    };
    for (int step = 0; step < 120 && (scenario.mirrored() != 202 || kAimed().isNull()); ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.mirrored() == 202);
    RAWFRAME_EXPECT(!kAimed().isNull());
    for (std::size_t index = 0; index < props.size(); index += 2) {
        RAWFRAME_EXPECT(scenario.serverWorld.destroy(props[index]).has_value());
    }
    int retiring = 0;
    for (int step = 0; step < 60; ++step) {
        const std::uint64_t kBefore = scenario.serverTransport->statistics().streamBytesSent;
        scenario.step(Steer{});
        const std::uint64_t kSent = scenario.serverTransport->statistics().streamBytesSent - kBefore;
        RAWFRAME_EXPECT(kSent <= kBudget);
        retiring += kSent > 0 ? 1 : 0;
        if (step == 1) {
            RAWFRAME_EXPECT(scenario.mirrored() > 102);
            RAWFRAME_EXPECT(kAimed().isNull());
        }
    }
    RAWFRAME_EXPECT(retiring >= 3);
    RAWFRAME_EXPECT(scenario.mirrored() == 102);
    RAWFRAME_EXPECT(kAimed().isNull());
}

RAWFRAME_TEST(WhatComesBackWhileItsRetirementWaitsIsDeclaredOnceItWent) {
    // Two hundred props near the player, every other one on either side of
    // it. The player steps away from one side and straight back: most of
    // that side's props are still retiring when they are in interest
    // again, and each is declared anew only once its retirement went, so
    // the client never holds one twice and ends up mirroring each once.
    // Whether retired or still retiring, each counts as seen until it left
    // alone.
    Scenario scenario{{}, {.mappingBytesPerPublish = kBudget, .interest = nearby()}};
    const auto kPosition = *scenario.schema->key<Position>();
    std::vector<world::EntityHandle> leaving;
    for (int index = 0; index < 200; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{index % 2 == 0 ? 5.0F : -5.0F, 0}).has_value());
        if (index % 2 == 0) {
            leaving.push_back(kProp);
        }
    }
    const auto kPlaceAt = [&](float x, int steps) {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        if (!kPlayer.isNull()) {
            *scenario.serverWorld.get(kPlayer, kPosition) = Position{x, 0};
        }
        for (int step = 0; step < steps; ++step) {
            scenario.step(Steer{});
            RAWFRAME_EXPECT(scenario.mirrored() <= 201);
        }
    };
    kPlaceAt(0, 60);
    kPlaceAt(0, 60);
    RAWFRAME_EXPECT(scenario.othersAt().size() == 200);
    const std::uint64_t kInView = scenario.tick.value - 1;
    kPlaceAt(-10, 2);
    RAWFRAME_EXPECT(scenario.mirrored() > 101);
    for (const world::EntityHandle kProp : leaving) {
        RAWFRAME_EXPECT(scenario.server->sentSince(1, kProp, kInView).has_value());
        RAWFRAME_EXPECT(!scenario.server->sentSince(1, kProp, scenario.tick.value).has_value());
    }
    kPlaceAt(0, 120);
    RAWFRAME_EXPECT(!scenario.client->ended());
    RAWFRAME_EXPECT(scenario.othersAt().size() == 200);
    RAWFRAME_EXPECT(scenario.server->statistics().interestLeft == 100);
}

RAWFRAME_TEST(AnEntityNamedByARetiringMappingIsNoEntityToTheServer) {
    // A player aims at a prop, naming it by the client's mapping for it,
    // and then steps away from the hundred props on that side at once.
    // While the prop's retirement waits the client still holds it and
    // keeps aiming at it, but the server reads that as aiming at nothing,
    // as it would once retired: what a client names is never an entity
    // out of its interest.
    Scenario scenario{{}, {.mappingBytesPerPublish = kBudget, .interest = nearby(), .aiming = true}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kAim = *scenario.schema->key<Aim>();
    // The last prop on the far side is retired last, and stands furthest
    // along.
    world::EntityHandle last;
    for (int index = 0; index < 200; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        const float kX = index % 2 == 0 ? 5 + (static_cast<float>(index) / 1000) : -5.0F;
        RAWFRAME_EXPECT(scenario.serverWorld.insert(kProp, kPosition, Position{kX, 0}).has_value());
        last = index % 2 == 0 ? kProp : last;
    }
    const auto kMirrorOfLast = [&] {
        world::EntityHandle found;
        float furthest = 0;
        auto query = world::Query<world::Read<Position>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&](world::EntityHandle entity, const Position& position) {
            if (entity != scenario.client->owned() && position.x > furthest) {
                furthest = position.x;
                found = entity;
            }
        });
        return found;
    };
    const auto kAimedAt = [&] {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        const Aim* const kAimed = kPlayer.isNull() ? nullptr : scenario.serverWorld.get(kPlayer, kAim);
        return kAimed != nullptr ? kAimed->target : world::EntityHandle{};
    };
    const auto kPlaceAt = [&](float x, int steps) {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        if (!kPlayer.isNull()) {
            *scenario.serverWorld.get(kPlayer, kPosition) = Position{x, 0};
        }
        for (int step = 0; step < steps; ++step) {
            scenario.step(Aim{.target = kMirrorOfLast(), .range = 1});
        }
    };
    kPlaceAt(0, 120);
    RAWFRAME_EXPECT(scenario.othersAt().size() == 200);
    RAWFRAME_EXPECT(kAimedAt() == last);
    const world::EntityHandle kMirror = kMirrorOfLast();
    kPlaceAt(-10, 2);
    RAWFRAME_EXPECT(scenario.clientWorld.alive(kMirror));
    RAWFRAME_EXPECT(kMirrorOfLast() == kMirror);
    RAWFRAME_EXPECT(kAimedAt().isNull());
}

RAWFRAME_TEST(AnAcknowledgementOfWhatWasNotDeclaredInTurnStrikes) {
    // A client acknowledges each declaration whole and in turn. One that
    // claims more, every ID at once or a declaration a second time, is
    // struck, and the server walks nothing for it.
    Scenario scenario{{}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int index = 0; index < 100; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index), 0}).has_value());
    }
    for (int step = 0; step < 60; ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.mirrored() == 101 && scenario.server->statistics().strikes == 0);
    const std::uint64_t kEpoch =
        scenario.client->accept().has_value() ? scenario.client->accept()->replicationEpoch : 0;
    const auto kAcknowledge = [&](std::uint32_t first, std::uint32_t count) {
        std::array<std::byte, 32> bytes{};
        network::Writer writer{bytes};
        RAWFRAME_EXPECT(
            world_replication::encodeMappingDeclaration(
                writer,
                {.replicationEpoch = kEpoch, .run = {.first = world_replication::NetEntityId{first}, .count = count}})
                .has_value());
        static_cast<void>(scenario.clientSessions->sendFrame(
            network::ConnectionId{1}, network::ControlFrame::MappingAck, writer.written()));
        for (int step = 0; step < 5; ++step) {
            scenario.step(Steer{});
        }
    };
    kAcknowledge(1, 4'000'000'000U);
    RAWFRAME_EXPECT(scenario.server->statistics().strikes == 1);
    kAcknowledge(1, 101);
    RAWFRAME_EXPECT(scenario.server->statistics().strikes == 2);
    RAWFRAME_EXPECT(scenario.firstPlayerPosition() != nullptr && scenario.mirrored() == 101);
}
