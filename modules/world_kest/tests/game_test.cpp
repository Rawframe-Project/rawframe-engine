// A whole Kest-scripted game loaded through composition and ticked by the
// World: its systems, entities, physics, and doors. What a description
// parses to is game_description_test.cpp's.

#include "game_harness.h"
#include "rawframe/composition/composition.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/scene/scene.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/query.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_kest/replication.h"
#include "rawframe/world_kest/spawn_scene.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

RAWFRAME_TEST(AKestGameRunsInTheWorld) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(plan.has_value());
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "movers.game\n" +
                              "world.tick_rate = 10\n" + "world.maximum_ticks_per_iteration = 100\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    RAWFRAME_EXPECT(kConfiguration.has_value());
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(positions().size() == 4);

    // Two seconds at 10 Hz is 20 ticks.
    clock.advance(execution::MonotonicDuration::fromSeconds(2));
    composition.runHostPhase(composition::HostPhase::RunWorlds,
                             composition::HostFrame{.iteration = 0, .now = clock.now()});
    const auto kAfter = positions();
    RAWFRAME_EXPECT(kAfter.size() == 4);
    int drifted = 0;
    for (const auto& [x, y] : kAfter) {
        drifted += (x == 20.0F && y == 40.0F) ? 1 : 0;
    }
    RAWFRAME_EXPECT(drifted == 3);
    // The one at the edge crossed it on the first tick, turned, and came back:
    // 99.5 + 1, then 19 steps of -1.
    bool bounced = false;
    for (const auto& [x, y] : kAfter) {
        bounced = bounced || (x == 81.5F && y == 0.0F);
    }
    RAWFRAME_EXPECT(bounced);
    composition.stop();
    simulation = nullptr;
}

RAWFRAME_TEST(WithoutAGameNothingLoads) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kRegistrars,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{
        *plan,
        composition::HostServices{
            .clock = &clock, .scope = &root, .cpu = &test::cpuExecutor(), .blockingIo = &test::blockingIoExecutor()}};
    RAWFRAME_EXPECT(composition.start().has_value());
    composition.stop();
}

RAWFRAME_TEST(AReplicatedComponentMayNameEntities) {
    // An entity field crosses once, as the receiver's name for it; its
    // generation does not cross apart.
    const kest::TypeLayout kLink{.size = 12,
                                 .alignment = 4,
                                 .mark = 0,
                                 .fields = {{"next.slot", 0, kest::FieldKind::U32},
                                            {"next.generation", 4, kest::FieldKind::U32},
                                            {"hops", 8, kest::FieldKind::I32}}};
    const std::array<std::string, 1> kEntities = {"next"};
    constexpr auto kLinkId = schema::ComponentTypeId::fromText("5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84");
    const auto kCodec = world_kest::codecFor(kLinkId, kLink, kEntities);
    RAWFRAME_EXPECT(kCodec.has_value() && kCodec->valid() && kCodec->namesEntities() && kCodec->fields.size() == 2 &&
                    kCodec->fields[0].offset == 0 && kCodec->fields[0].kind == world_replication::WireKind::Entity &&
                    kCodec->wireSize() == 8);
    const auto kPlain = world_kest::codecFor(kLinkId, kLink, {});
    RAWFRAME_EXPECT(kPlain.has_value() && !kPlain->namesEntities() && kPlain->fields.size() == 3);

    // And a game that replicates one starts.
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kRegistrars,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    const auto kConfiguration = composition::Configuration::parse(std::string{"kest.game = "} +
                                                                  RAWFRAME_WORLD_KEST_GAMES + "sharedlinks.game\n");
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    RAWFRAME_EXPECT(composition.start().has_value());
    composition.stop();
}

RAWFRAME_TEST(AGameThatDoesNotLoadFailsTheStart) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kRegistrars,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    for (const std::string& kText : {std::string{"kest.game = /nonexistent/x.game\n"},
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "broken.game\n",
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "mispredicted.game\n",
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "misinterested.game\n",
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "mislinked.game\n"}) {
        const auto kConfiguration = composition::Configuration::parse(kText);
        composition::Composition composition{*plan,
                                             composition::HostServices{.clock = &clock,
                                                                       .scope = &root,
                                                                       .cpu = &test::cpuExecutor(),
                                                                       .blockingIo = &test::blockingIoExecutor(),
                                                                       .configuration = &*kConfiguration}};
        RAWFRAME_EXPECT(!composition.start().has_value());
    }
}

namespace {

/// Every bullet's x, in iteration order.
std::vector<float> bullets() {
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("91d4e2b8-5a7c-4f03-8e16-2b9c0d7a4e53"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<float> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        const auto* values = reinterpret_cast<const float*>(chunk.columns[0]);
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            found.push_back(values[row * 2]);
        }
    });
    return found;
}

} // namespace

RAWFRAME_TEST(KestSystemsCreateAndDestroyEntities) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "shooter.game\n" +
                              "world.tick_rate = 10\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    std::vector<std::size_t> alive;
    for (std::uint64_t tick = 0; tick < 20; ++tick) {
        clock.advance(execution::MonotonicDuration::fromMilliseconds(100));
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
        alive.push_back(bullets().size());
    }
    // Each gun fires every fourth tick from the first; a bullet exists from
    // that tick's barrier, moves 10 a tick from the next, and is destroyed at
    // the barrier of its sixth move, which takes it past 50.
    const std::vector<std::size_t> kExpected = {2, 2, 2, 2, 4, 4, 2, 2, 4, 4, 2, 2, 4, 4, 2, 2, 4, 4, 2, 2};
    RAWFRAME_EXPECT(alive == kExpected);
    const auto kLast = bullets();
    RAWFRAME_EXPECT(kLast.size() == 2 && kLast[0] == 30.0F && kLast[1] == 30.0F);
    composition.stop();
    simulation = nullptr;
}

namespace {

const std::array<composition::RegistrarEntry, 5> kWithPhysics = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics2d", &physics2d::registerParticipants, physics2d::kScopes},
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

/// Every body's pose and velocity, in entity order, after each of `ticks`
/// ticks of the crates game at 60 Hz.
std::vector<std::vector<std::pair<physics2d::Pose2D, physics2d::Velocity2D>>> playCrates(int ticks) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(plan.has_value());
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "crates.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return {};
    }
    std::vector<std::vector<std::pair<physics2d::Pose2D, physics2d::Velocity2D>>> seen;
    for (int tick = 0; tick < ticks; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(
            composition::HostPhase::RunWorlds,
            composition::HostFrame{.iteration = static_cast<std::uint64_t>(tick), .now = clock.now()});
        world::World& world = *simulation->world();
        auto query =
            world::Query<world::Read<physics2d::Pose2D>, world::Read<physics2d::Velocity2D>>::resolve(world.registry());
        std::vector<std::pair<world::EntityHandle, std::pair<physics2d::Pose2D, physics2d::Velocity2D>>> bodies;
        query->forEach(
            world,
            [&](world::EntityHandle entity, const physics2d::Pose2D& pose, const physics2d::Velocity2D& velocity) {
                bodies.push_back({entity, {pose, velocity}});
            });
        std::sort(bodies.begin(), bodies.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        seen.emplace_back();
        for (const auto& [entity, state] : bodies) {
            seen.back().push_back(state);
        }
    }
    composition.stop();
    simulation = nullptr;
    return seen;
}

} // namespace

RAWFRAME_TEST(AGameWithPhysicsStepsItsBodies) {
    const auto kFirst = playCrates(300);
    RAWFRAME_EXPECT(kFirst.size() == 300 && kFirst.back().size() == 3);
    if (kFirst.size() != 300 || kFirst.back().size() != 3) {
        return;
    }
    // The ground stays; the crate and the ball fall onto it, come to rest at
    // one meter, and are kicked up again by the Kest system, over and over.
    RAWFRAME_EXPECT(kFirst.back()[0].first.y == 0);
    int kicked = 0;
    for (std::size_t tick = 1; tick < kFirst.size(); ++tick) {
        for (std::size_t body = 1; body < 3; ++body) {
            const bool kWasResting = kFirst[tick - 1][body].first.y < 1.1;
            kicked += kWasResting && kFirst[tick][body].second.y > 2 ? 1 : 0;
            RAWFRAME_EXPECT(kFirst[tick][body].first.y > 0.9);
        }
    }
    RAWFRAME_EXPECT(kicked >= 4);
    // Played again, every bit the same.
    const auto kSecond = playCrates(300);
    RAWFRAME_EXPECT(kSecond.size() == kFirst.size());
    bool same = kSecond.size() == kFirst.size();
    for (std::size_t tick = 0; same && tick < kFirst.size(); ++tick) {
        same = kFirst[tick].size() == kSecond[tick].size();
        // Field by field: a pair's padding is not state.
        for (std::size_t body = 0; same && body < kFirst[tick].size(); ++body) {
            same = std::memcmp(&kFirst[tick][body].first, &kSecond[tick][body].first, sizeof(physics2d::Pose2D)) == 0 &&
                   std::memcmp(
                       &kFirst[tick][body].second, &kSecond[tick][body].second, sizeof(physics2d::Velocity2D)) == 0;
        }
    }
    RAWFRAME_EXPECT(same);
}

RAWFRAME_TEST(AKestSystemShootsBackInTime) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "sniper.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < 90; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    // The target passes the spot around tick 29 and is gone by tick 35; the
    // scope, shooting at tick 30's world, hits on every tick from the one
    // after tick 30 was stepped, besides the few before it when the moment
    // was still ahead and so clamped to now.
    world::World& world = *simulation->world();
    const auto kScope =
        world.registry().find(schema::ComponentTypeId::fromText("4e18ba81-c515-47e7-94a6-abd288fa6c60"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kScope, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::int32_t hits = -1;
    query->forEachChunk(world, [&hits](const world::ColumnChunk& chunk) {
        std::memcpy(&hits, chunk.columns[0] + 8, sizeof hits);
    });
    RAWFRAME_EXPECT(hits >= 59 && hits <= 66);
    composition.stop();
    simulation = nullptr;
}

RAWFRAME_TEST(AKestSystemAsksThreeDimensionalPhysics) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "towers.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < 120; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    // Both have fallen onto the floor, whose top is half a meter up, and
    // the probe under each found it on every tick after a step; a meter
    // about each overlaps the body and the floor.
    world::World& world = *simulation->world();
    const auto kProbe =
        world.registry().find(schema::ComponentTypeId::fromText("478adedf-3aad-4510-8073-dc421516ce7e"));
    auto query = *world::Query<world::Read<physics3d::Pose3D>>::resolve(world.registry());
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kProbe, world::Access::Read}};
    auto probes = world::ColumnQuery::resolve(kTerms, world.registry());
    int resting = 0;
    query.forEach(world, [&resting](world::EntityHandle, const physics3d::Pose3D& pose) {
        resting += pose.y > 0.8 && pose.y < 1.05 ? 1 : 0;
    });
    RAWFRAME_EXPECT(resting == 2);
    int found = 0;
    probes->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            double floor = 0;
            std::int32_t ticks = 0;
            std::memcpy(&floor, chunk.columns[0] + (row * 16), sizeof floor);
            std::memcpy(&ticks, chunk.columns[0] + (row * 16) + 8, sizeof ticks);
            std::uint32_t crowd = 0;
            std::memcpy(&crowd, chunk.columns[0] + (row * 16) + 12, sizeof crowd);
            found += std::abs(floor - 0.5) < 1e-4 && ticks == 120 && crowd == 2 ? 1 : 0;
        }
    });
    RAWFRAME_EXPECT(found == 2);
    composition.stop();
    simulation = nullptr;
}

RAWFRAME_TEST(AKestProgramHangsABarOnAHinge) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "hinged.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < 90; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    // The bar has swung down about z, its middle half a meter from the
    // post's, and made one joint.
    world::World& world = *simulation->world();
    auto bodies =
        *world::Query<world::Read<physics3d::Body3D>, world::Read<physics3d::Pose3D>>::resolve(world.registry());
    int swung = 0;
    bodies.forEach(world, [&swung](world::EntityHandle, const physics3d::Body3D& body, const physics3d::Pose3D& pose) {
        const double kFromPost = std::sqrt((pose.x * pose.x) + ((pose.y - 5) * (pose.y - 5)) + (pose.z * pose.z));
        swung +=
            body.motion == 2 && std::abs(kFromPost - 0.5) < 0.02 && pose.y < 4.9 && std::abs(pose.z) < 0.01 ? 1 : 0;
    });
    RAWFRAME_EXPECT(swung == 1);
    auto joints = *world::Query<world::Read<physics3d::Joint3D>>::resolve(world.registry());
    int made = 0;
    joints.forEach(world, [&made](world::EntityHandle, const physics3d::Joint3D&) {
        ++made;
    });
    RAWFRAME_EXPECT(made == 1);
    // The lamp hangs a meter from the post, where the bar's far end is.
    auto lamps =
        *world::Query<world::Read<physics3d::Attach3D>, world::Read<physics3d::Pose3D>>::resolve(world.registry());
    int hanging = 0;
    lamps.forEach(world, [&hanging](world::EntityHandle, const physics3d::Attach3D&, const physics3d::Pose3D& pose) {
        const double kFromPost = std::sqrt((pose.x * pose.x) + ((pose.y - 5) * (pose.y - 5)) + (pose.z * pose.z));
        hanging += std::abs(kFromPost - 1) < 0.03 ? 1 : 0;
    });
    RAWFRAME_EXPECT(hanging == 1);
    composition.stop();
    simulation = nullptr;
}

RAWFRAME_TEST(AKestProgramHangsABarOnAHingeInTwoDimensions) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "lever.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < 90; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    // The bar has swung down, its middle half a meter from the post's, and
    // made one joint.
    world::World& world = *simulation->world();
    auto bodies =
        *world::Query<world::Read<physics2d::Body2D>, world::Read<physics2d::Pose2D>>::resolve(world.registry());
    int swung = 0;
    bodies.forEach(world, [&swung](world::EntityHandle, const physics2d::Body2D& body, const physics2d::Pose2D& pose) {
        const double kFromPost = std::sqrt((pose.x * pose.x) + ((pose.y - 5) * (pose.y - 5)));
        swung += body.motion == 2 && std::abs(kFromPost - 0.5) < 0.02 && pose.y < 4.9 ? 1 : 0;
    });
    RAWFRAME_EXPECT(swung == 1);
    auto joints = *world::Query<world::Read<physics2d::Joint2D>>::resolve(world.registry());
    int made = 0;
    joints.forEach(world, [&made](world::EntityHandle, const physics2d::Joint2D&) {
        ++made;
    });
    RAWFRAME_EXPECT(made == 1);
    composition.stop();
    simulation = nullptr;
}

RAWFRAME_TEST(ARunnerHitIsToldSo) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_SAMPLE_GAMES + "runners/duel.game\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    auto started = composition.start();
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < 90; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    // A shot every 21 ticks from the first: five in 90 ticks. The first is
    // fired before physics has made a body, and meets nothing; the other
    // four meet the mark, which is told of each (a tick later, once its Hit
    // is in).
    world::World& world = *simulation->world();
    const auto kScore =
        world.registry().find(schema::ComponentTypeId::fromText("f6453556-fcdd-46e8-9208-866fade389cf"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kScore, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::array<std::int32_t, 4>> scores;
    query->forEachChunk(world, [&scores](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            std::array<std::int32_t, 4>& score = scores.emplace_back();
            std::memcpy(score.data(), chunk.columns[0] + (row * sizeof score), sizeof score);
        }
    });
    // Shots, hits, cooldown, taken.
    const bool kShooterFirst = scores.size() == 2 && scores[0][0] != 0;
    RAWFRAME_EXPECT(scores.size() == 2);
    if (scores.size() == 2) {
        const auto& kShooter = scores[kShooterFirst ? 0 : 1];
        const auto& kMark = scores[kShooterFirst ? 1 : 0];
        RAWFRAME_EXPECT(kShooter[0] == 5 && kShooter[1] == 4 && kShooter[3] == 0);
        RAWFRAME_EXPECT(kMark[0] == 0 && kMark[1] == 0 && kMark[3] == 4);
    }
    composition.stop();
    simulation = nullptr;
}

namespace {

/// Rewrites the program and moves its time on, so the change is seen however
/// coarse the file system's clock is.
void rewrite(const std::filesystem::path& path, std::string_view text, int step) {
    writeText(path, text);
    std::error_code error;
    const auto kWritten = std::filesystem::last_write_time(path, error);
    std::filesystem::last_write_time(path, kWritten + std::chrono::seconds{step}, error);
}

} // namespace

RAWFRAME_TEST(AChangedProgramReloadsBetweenTicks) {
    std::error_code error;
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path(error) /
        ("rawframe-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(kDirectory, error);
    std::filesystem::create_directories(kDirectory, error);
    const std::string kSource = readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.kest");
    writeText(kDirectory / "movers.kest", kSource);
    writeText(kDirectory / "movers.game", readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.game"));

    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText =
        "kest.game = " + (kDirectory / "movers.game").string() + "\nkest.reload_every = 1\nworld.tick_rate = 10\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    RAWFRAME_EXPECT(composition.start().has_value());
    std::uint64_t iteration = 0;
    const auto kIterate = [&] {
        clock.advance(execution::MonotonicDuration::fromMilliseconds(100));
        const composition::HostFrame kFrame{.iteration = iteration++, .now = clock.now()};
        composition.runHostPhase(composition::HostPhase::RunWorlds, kFrame);
        composition.runHostPhase(composition::HostPhase::Maintenance, kFrame);
    };
    const auto kFirstX = [] {
        return positions().front().first;
    };
    kIterate();
    kIterate();
    RAWFRAME_EXPECT(kFirstX() == 2.0F);

    // Movers now drift backwards; the World keeps its positions.
    std::string backwards = kSource;
    const std::string kForward = "positions[i].x + velocities[i].dx";
    backwards.replace(backwards.find(kForward), kForward.size(), "positions[i].x - velocities[i].dx");
    rewrite(kDirectory / "movers.kest", backwards, 5);
    kIterate(); // ticks forward once more, then reloads in maintenance
    RAWFRAME_EXPECT(kFirstX() == 3.0F);
    kIterate();
    RAWFRAME_EXPECT(kFirstX() == 2.0F);

    // A program that does not compile is refused and the last one runs on.
    rewrite(kDirectory / "movers.kest", "module movers\nfn broken( {\n", 10);
    kIterate();
    kIterate();
    RAWFRAME_EXPECT(kFirstX() == 0.0F);
    composition.stop();
    simulation = nullptr;
    std::filesystem::remove_all(kDirectory, error);
}

namespace {

struct LinkRow {
    world::EntityHandle entity;
    world::EntityHandle next;
    std::int32_t hops = 0;
};

/// Every link: its entity, the entity it names, and its hop count.
std::vector<LinkRow> links() {
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<LinkRow> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            LinkRow link{.entity = chunk.entities[row]};
            std::memcpy(&link.next, chunk.columns[0] + (row * 12), sizeof link.next);
            std::memcpy(&link.hops, chunk.columns[0] + (row * 12) + 8, sizeof link.hops);
            found.push_back(link);
        }
    });
    return found;
}

} // namespace

RAWFRAME_TEST(CheckpointsCarryEntityReferences) {
    const std::filesystem::path kDirectory = test::scratchDirectory("checkpoint");
    std::filesystem::create_directories(kDirectory);
    const std::string kGame = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES +
                              "linked.game\nworld.tick_rate = 10\nworld.maximum_ticks_per_iteration = 100\n";
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(plan.has_value());
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    const auto kRun = [&](const std::string& extra) {
        const auto kConfiguration = composition::Configuration::parse(kGame + extra);
        composition::Composition composition{*plan,
                                             composition::HostServices{.clock = &clock,
                                                                       .scope = &root,
                                                                       .cpu = &test::cpuExecutor(),
                                                                       .blockingIo = &test::blockingIoExecutor(),
                                                                       .configuration = &*kConfiguration}};
        RAWFRAME_EXPECT(composition.start().has_value());
        // Ten ticks are due; the capture holds the World at tick 5.
        clock.advance(execution::MonotonicDuration::fromSeconds(1));
        const composition::HostFrame kFrame{.iteration = 0, .now = clock.now()};
        composition.runHostPhase(composition::HostPhase::RunWorlds, kFrame);
        composition.runHostPhase(composition::HostPhase::Maintenance, kFrame);
        return std::pair{simulation->tick().value, links()};
    };

    const auto [kCaptured, kBefore] =
        kRun("checkpoint.capture_ticks = 5\ncheckpoint.capture_prefix = " + (kDirectory / "c-").string() + "\n");
    RAWFRAME_EXPECT(kCaptured == 5 && kBefore.size() == 3);
    RAWFRAME_EXPECT(std::filesystem::exists(kDirectory / "c-5.rfsn"));

    // A World restored from it: three links, each naming another live link,
    // around a cycle of three, five hops each, at tick 5.
    const auto kConfiguration =
        composition::Configuration::parse(kGame + "checkpoint.restore = " + (kDirectory / "c-5.rfsn").string() + "\n");
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    RAWFRAME_EXPECT(composition.start().has_value());
    RAWFRAME_EXPECT(simulation->tick().value == 5);
    const std::vector<LinkRow> kAfter = links();
    RAWFRAME_EXPECT(kAfter.size() == 3);
    for (const LinkRow& link : kAfter) {
        RAWFRAME_EXPECT(link.hops == 5 && simulation->world()->alive(link.next) && !(link.next == link.entity));
        world::EntityHandle at = link.entity;
        for (int hop = 0; hop < 3; ++hop) {
            const auto kFound = std::find_if(kAfter.begin(), kAfter.end(), [&](const LinkRow& each) {
                return each.entity == at;
            });
            at = kFound == kAfter.end() ? world::EntityHandle{} : kFound->next;
        }
        RAWFRAME_EXPECT(at == link.entity);
    }
    composition.stop();
    simulation = nullptr;
    std::filesystem::remove_all(kDirectory);
}
