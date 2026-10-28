// The mappings a connection holds, over loopback: a join in view of many
// entities is declared, and a mass removal retired, over several publishes
// within the mapping budget; while a retirement waits, its entity is sent
// nothing, named by nothing, and declared anew only once it went.

#include "replication_scenario.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace rawframe;
using namespace rawframe::scenario;

RAWFRAME_TEST(AJoinDeclaresItsEntitiesWithinTheMappingBudget) {
    // A hundred props in view and room for about a dozen declarations a
    // publish: the join declares them over several publishes, none of which
    // sends the connection more mapping bytes than the budget, and the
    // client ends up mirroring every one.
    constexpr std::size_t kBudget = 200;
    Scenario scenario{{}, {.mappingBytesPerPublish = kBudget}};
    const auto kPosition = *scenario.schema->key<Position>();
    for (int index = 0; index < 100; ++index) {
        const world::EntityHandle kProp = *scenario.serverWorld.create();
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(kProp, kPosition, Position{static_cast<float>(index), 0}).has_value());
    }
    // The publish that admits also sends the accept; the ones after it send
    // mappings alone.
    while (!scenario.client->admitted()) {
        scenario.step(Steer{});
    }
    int declaring = 0;
    for (int step = 0; step < 60; ++step) {
        const std::uint64_t kBefore = scenario.serverTransport->statistics().streamBytesSent;
        scenario.step(Steer{});
        const std::uint64_t kSent = scenario.serverTransport->statistics().streamBytesSent - kBefore;
        RAWFRAME_EXPECT(kSent <= kBudget);
        declaring += kSent > 0 ? 1 : 0;
    }
    RAWFRAME_EXPECT(declaring >= 5);
    RAWFRAME_EXPECT(scenario.mirrored() == 101);
    RAWFRAME_EXPECT(scenario.othersAt().size() == 100);
}

RAWFRAME_TEST(AMassRemovalRetiresItsMappingsWithinTheMappingBudget) {
    // A hundred props mirrored, then all destroyed in one tick: their
    // retirements go over several publishes, none of which sends the
    // connection more mapping bytes than the budget, and the client ends
    // up holding its player and a turret alone. The turret aims at the last
    // prop, whose retirement waits longest: from the removal on, the client
    // sees it aim at nothing, not at the prop it still holds.
    constexpr std::size_t kBudget = 200;
    Scenario scenario{{}, {.mappingBytesPerPublish = kBudget}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kAim = *scenario.schema->key<Aim>();
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 100; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(props.back(), kPosition, Position{static_cast<float>(index), 0}).has_value());
    }
    const world::EntityHandle kTurret = *scenario.serverWorld.create();
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kPosition, Position{-1, 0}).has_value());
    RAWFRAME_EXPECT(scenario.serverWorld.insert(kTurret, kAim, Aim{.target = props.back(), .range = 5}).has_value());
    const auto kAimed = [&] {
        world::EntityHandle target;
        auto query = world::Query<world::Read<Aim>>::resolve(*scenario.schema);
        query->forEach(scenario.clientWorld, [&target](world::EntityHandle, const Aim& aim) {
            target = aim.target;
        });
        return target;
    };
    for (int step = 0; step < 120 && (scenario.mirrored() != 102 || kAimed().isNull()); ++step) {
        scenario.step(Steer{});
    }
    RAWFRAME_EXPECT(scenario.mirrored() == 102);
    RAWFRAME_EXPECT(!kAimed().isNull());
    for (const world::EntityHandle kProp : props) {
        RAWFRAME_EXPECT(scenario.serverWorld.destroy(kProp).has_value());
    }
    int retiring = 0;
    for (int step = 0; step < 60; ++step) {
        const std::uint64_t kBefore = scenario.serverTransport->statistics().streamBytesSent;
        scenario.step(Steer{});
        const std::uint64_t kSent = scenario.serverTransport->statistics().streamBytesSent - kBefore;
        RAWFRAME_EXPECT(kSent <= kBudget);
        retiring += kSent > 0 ? 1 : 0;
        if (step == 3) {
            RAWFRAME_EXPECT(scenario.mirrored() > 2);
            RAWFRAME_EXPECT(kAimed().isNull());
        }
    }
    RAWFRAME_EXPECT(retiring >= 5);
    RAWFRAME_EXPECT(scenario.mirrored() == 2);
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>{-1});
}

RAWFRAME_TEST(WhatComesBackWhileItsRetirementWaitsIsDeclaredOnceItWent) {
    // A hundred props near the player and room for about a dozen mapping
    // frames a publish. The player steps away and straight back: most of
    // the props are still retiring when they are in interest again, and
    // each is declared anew only once its retirement went, so the client
    // never holds one twice and ends up mirroring each once. Whether
    // retired or still retiring, each counts as seen until it left alone.
    constexpr std::size_t kBudget = 200;
    Scenario scenario{{},
                      {.mappingBytesPerPublish = kBudget,
                       .interest = world_replication::InterestSettings{
                           .position = Position::kComponentTypeId,
                           .axes = {{offsetof(Position, x), WireKind::F32}, {offsetof(Position, y), WireKind::F32}},
                           .radius = 10,
                           .leaveRadius = 12}}};
    const auto kPosition = *scenario.schema->key<Position>();
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 100; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(scenario.serverWorld.insert(props.back(), kPosition, Position{5, 0}).has_value());
    }
    const auto kPlaceAt = [&](float x, int steps) {
        const world::EntityHandle kPlayer = scenario.server->player(network::ConnectionId{1});
        if (!kPlayer.isNull()) {
            *scenario.serverWorld.get(kPlayer, kPosition) = Position{x, 0};
        }
        for (int step = 0; step < steps; ++step) {
            scenario.step(Steer{});
            RAWFRAME_EXPECT(scenario.mirrored() <= 101);
        }
    };
    kPlaceAt(0, 60);
    kPlaceAt(0, 60);
    RAWFRAME_EXPECT(scenario.othersAt().size() == 100);
    const std::uint64_t kInView = scenario.tick.value - 1;
    kPlaceAt(1000, 2);
    RAWFRAME_EXPECT(scenario.mirrored() > 1);
    for (const world::EntityHandle kProp : props) {
        RAWFRAME_EXPECT(scenario.server->sentSince(1, kProp, kInView).has_value());
        RAWFRAME_EXPECT(!scenario.server->sentSince(1, kProp, scenario.tick.value).has_value());
    }
    kPlaceAt(0, 120);
    RAWFRAME_EXPECT(!scenario.client->ended());
    RAWFRAME_EXPECT(scenario.othersAt() == std::vector<float>(100, 5.0F));
    RAWFRAME_EXPECT(scenario.server->statistics().interestLeft == 100);
}

RAWFRAME_TEST(AnEntityNamedByARetiringMappingIsNoEntityToTheServer) {
    // A player aims at a prop, naming it by the client's mapping for it,
    // and then steps away from a hundred props at once with room for
    // about a dozen mapping frames a publish. While the prop's retirement
    // waits the client still holds it and keeps aiming at it, but the
    // server reads that as aiming at nothing, as it would once retired:
    // what a client names is never an entity out of its interest.
    constexpr std::size_t kBudget = 200;
    Scenario scenario{{},
                      {.mappingBytesPerPublish = kBudget,
                       .interest = world_replication::InterestSettings{.position = Position::kComponentTypeId,
                                                                       .axes = {{offsetof(Position, x), WireKind::F32},
                                                                                {offsetof(Position, y), WireKind::F32}},
                                                                       .radius = 10,
                                                                       .leaveRadius = 12},
                       .aiming = true}};
    const auto kPosition = *scenario.schema->key<Position>();
    const auto kAim = *scenario.schema->key<Aim>();
    // The last prop is retired last, and stands furthest along.
    std::vector<world::EntityHandle> props;
    for (int index = 0; index < 100; ++index) {
        props.push_back(*scenario.serverWorld.create());
        RAWFRAME_EXPECT(
            scenario.serverWorld.insert(props.back(), kPosition, Position{5 + (static_cast<float>(index) / 100), 0})
                .has_value());
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
    RAWFRAME_EXPECT(scenario.othersAt().size() == 100);
    RAWFRAME_EXPECT(kAimedAt() == props.back());
    const world::EntityHandle kMirror = kMirrorOfLast();
    kPlaceAt(1000, 4);
    RAWFRAME_EXPECT(scenario.clientWorld.alive(kMirror));
    RAWFRAME_EXPECT(kMirrorOfLast() == kMirror);
    RAWFRAME_EXPECT(kAimedAt().isNull());
}
