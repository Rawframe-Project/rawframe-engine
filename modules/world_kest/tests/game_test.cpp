// Game descriptions: what parses, what is refused and where, and a whole
// Kest-scripted game loaded through composition and ticked by the World.

#include "game_harness.h"
#include "rawframe/composition/composition.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/scene/scene.h"
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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;
using world_kest::parseGame;
using world_kest::WorldKestError;

namespace {

bool refusedAt(std::string_view text, WorldKestError error, std::string_view line) {
    auto game = parseGame(text);
    if (game.has_value() || game.error().code() != world_kest::code(error)) {
        return false;
    }
    for (const auto& field : game.error().context()) {
        if (field.key == "line") {
            return field.value == line;
        }
    }
    return false;
}

} // namespace

RAWFRAME_TEST(AGameDescriptionParses) {
    auto game = parseGame("# movers\n"
                          "program movers.kest\n"
                          "\n"
                          "system a.move simulation integrate entities write a.position read a.velocity after a.input "
                          "random drift\n"
                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position  # later is fine\n"
                          "component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 a.velocity Velocity\r\n"
                          "spawn 2 a.position x=1 a.velocity\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->program == "movers.kest");
    // The two declared, then the engine's persistent identity, which any
    // entity may carry.
    RAWFRAME_EXPECT(game->components.size() == 3 && game->components[1].kestType == "Velocity" &&
                    game->components[2].name == "rawframe.world.persistent");
    RAWFRAME_EXPECT(game->systems.size() == 1 && game->systems[0].columns.size() == 3);
    RAWFRAME_EXPECT(game->systems[0].columns[0].entities);
    RAWFRAME_EXPECT(game->systems[0].columns[1].access == world::Access::Write);
    RAWFRAME_EXPECT((game->systems[0].randomStreams == std::vector<std::string>{"drift"}));
    RAWFRAME_EXPECT((game->systems[0].after == std::vector<std::string>{"a.input"}));
    RAWFRAME_EXPECT(game->spawns.size() == 1 && game->spawns[0].count == 2);
    RAWFRAME_EXPECT(game->spawns[0].components.size() == 2 && game->spawns[0].components[0].fields.size() == 1);
}

RAWFRAME_TEST(PredictionIsDeclaredByLine) {
    auto game = parseGame("program p.kest\n"
                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position\n"
                          "system a.move simulation move write a.position predicted\n"
                          "system a.other simulation other predicted write a.position\n"
                          "system a.server simulation serve read a.position\n"
                          "predict a.position\n"
                          "interpolate a.position\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->systems[0].predicted && game->systems[1].predicted && !game->systems[2].predicted);
    RAWFRAME_EXPECT(game->systems[0].columns.size() == 1 && game->systems[1].columns.size() == 1);
    RAWFRAME_EXPECT((game->predicted == std::vector<std::string>{"a.position"}));
    RAWFRAME_EXPECT((game->interpolated == std::vector<std::string>{"a.position"}));
    RAWFRAME_EXPECT(refusedAt("program p.kest\npredict\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\ninterpolate\n", WorldKestError::BadGameLine, "2"));
}

RAWFRAME_TEST(ControlsAreDeclaredByLine) {
    constexpr std::string_view kHead = "program p.kest\n"
                                       "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.stick Stick\n";
    auto game = parseGame(std::string{kHead} + "input a.stick\nactions a.actions\nsample sample.kest sample\n");
    RAWFRAME_EXPECT(game.has_value() && game->controls.has_value() && game->controls->actions == "a.actions" &&
                    game->controls->program == "sample.kest" && game->controls->entry == "sample");
    RAWFRAME_EXPECT(parseGame(kHead).has_value() && !parseGame(kHead)->controls.has_value());
    // Together, once each, and with an input line.
    RAWFRAME_EXPECT(
        refusedAt(std::string{kHead} + "input a.stick\nactions a.actions\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kHead} + "actions a.actions\nsample s.kest f\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kHead} + "input a.stick\nactions a b\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kHead} + "input a.stick\nsample s.kest\n", WorldKestError::BadGameLine, "4"));
}

RAWFRAME_TEST(AudioIsDeclaredByLine) {
    auto game = parseGame("program p.kest\nmixer game.mixer\nsound 00000000000000a1 steps.sound\n"
                          "sound 00000000000000a2 shot.sound\n");
    RAWFRAME_EXPECT(game.has_value() && game->audio.has_value() && game->audio->mixer == "game.mixer" &&
                    game->audio->sounds.size() == 2 && game->audio->sounds[1].id == 0xa2 &&
                    game->audio->sounds[1].path == "shot.sound");
    RAWFRAME_EXPECT(!parseGame("program p.kest\n")->audio.has_value());
    RAWFRAME_EXPECT(refusedAt("program p.kest\nsound 00000000000000a1 a.sound\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nsound a1 a.sound\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nsound 00000000000000a1 a.sound\nsound 00000000000000a1 "
                              "b.sound\n",
                              WorldKestError::BadGameLine,
                              "4"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nmixer n\n", WorldKestError::BadGameLine, "3"));
}

RAWFRAME_TEST(InterestIsDeclaredByLine) {
    constexpr std::string_view kProgram = "program p.kest\n"
                                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position\n";
    auto game = parseGame(std::string{kProgram} + "interest a.position x y within 40.5\n");
    RAWFRAME_EXPECT(game.has_value() && game->interest.has_value());
    if (!game.has_value() || !game->interest.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->interest->component == "a.position" && game->interest->radius == 40.5);
    RAWFRAME_EXPECT((game->interest->axes == std::vector<std::string>{"x", "y"}));
    // No field, four fields, no radius, a radius that is not positive, a
    // second line, and an undeclared component.
    for (const std::string_view kLine : {"interest a.position within 4\n",
                                         "interest a.position x y z w within 4\n",
                                         "interest a.position x y\n",
                                         "interest a.position x within 0\n",
                                         "interest a.position x within nan\n",
                                         "interest a.position x within 4\n\ninterest a.position y within 4\n"}) {
        RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + std::string{kLine}, WorldKestError::BadGameLine, "3") ||
                        refusedAt(std::string{kProgram} + std::string{kLine}, WorldKestError::BadGameLine, "5"));
    }
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "interest a.velocity x within 4\n", WorldKestError::UnknownName, "3"));
}

RAWFRAME_TEST(PhysicsIsDeclaredByLine) {
    auto game = parseGame("program p.kest\nphysics2d gravity 0.5 -9.8 substeps 8\n"
                          "spawn 1 rawframe.physics2d.body width=1 rawframe.physics2d.pose y=2\n");
    RAWFRAME_EXPECT(game.has_value() && game->physics.has_value());
    if (!game.has_value() || !game->physics.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->physics->dimensions == 2 && game->physics->gravityX == 0.5F &&
                    game->physics->gravityY == -9.8F && game->physics->substeps == 8);
    // The engine's six components, under their engine names, and its
    // persistent identity.
    RAWFRAME_EXPECT(game->components.size() == 7 && game->components[0].name == "rawframe.physics2d.body" &&
                    game->components[1].kestType == "Pose2D");
    const auto kDefaults = parseGame("program p.kest\nphysics2d\n");
    RAWFRAME_EXPECT(kDefaults.has_value() && kDefaults->physics->gravityY == -10.0F &&
                    kDefaults->physics->substeps == 4);
    // In three dimensions: three numbers of gravity, and the six 3D
    // components.
    const auto kThree = parseGame("program p.kest\nphysics3d gravity 0 -9.8 1.5\n");
    RAWFRAME_EXPECT(kThree.has_value() && kThree->physics->dimensions == 3 && kThree->physics->gravityZ == 1.5F &&
                    kThree->components.size() == 7 && kThree->components[1].name == "rawframe.physics3d.pose");
    for (const std::string_view kLine : {"physics2d gravity 1\n",
                                         "physics2d spin 3\n",
                                         "physics2d substeps four\n",
                                         "physics2d\nphysics2d\n",
                                         "physics3d gravity 0 -10\n",
                                         "physics2d\nphysics3d\n"}) {
        const std::string kText = "program p.kest\n" + std::string{kLine};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "2") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "3"));
    }
}

RAWFRAME_TEST(CollisionIsDeclaredByLine) {
    const std::string kPhysics = "program p.kest\nphysics2d\n";
    auto game = parseGame(kPhysics + "collision class ball 3f1c9a7e52d04b18\ncollision class wall 00000000000000a1\n"
                                     "collision rule ball wall trigger\ncollision default ignore\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->collision.classes.size() == 2 && game->collision.classes[0].id == 0x3f1c9a7e52d04b18 &&
                    game->collision.classes[1].name == "wall" && game->collision.classes[1].id == 0xa1);
    RAWFRAME_EXPECT(game->collision.rules.size() == 1 &&
                    game->collision.rules[0].rule == physics::CollisionRule::Trigger &&
                    game->collision.fallback == physics::CollisionRule::Ignore);
    // A short identity, nought, no hex, an unknown rule, a second default,
    // a class not declared, and collision without physics.
    for (const std::string_view kLine : {"collision class ball 3f1c9a7e52d04b1\n",
                                         "collision class ball 0000000000000000\n",
                                         "collision class ball 3f1c9a7e52d04b1z\n",
                                         "collision class ball 3f1c9a7e52d04b18\ncollision rule ball ball bounce\n",
                                         "collision default ignore\ncollision default collide\n"}) {
        const std::string kText = kPhysics + std::string{kLine};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
    RAWFRAME_EXPECT(refusedAt(kPhysics + "collision rule ball wall ignore\n", WorldKestError::UnknownName, "3"));
    RAWFRAME_EXPECT(
        refusedAt("program p.kest\ncollision class ball 3f1c9a7e52d04b18\n", WorldKestError::BadGameLine, "2"));
}

RAWFRAME_TEST(BadLinesAreRefusedWhereTheyAre) {
    constexpr std::string_view kProgram = "program p.kest\n";
    RAWFRAME_EXPECT(refusedAt("program p.kest\nbuild x\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("component 1234 a.b T\n", WorldKestError::BadGameLine, "1"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nprogram q.kest\n", WorldKestError::BadGameLine, "2"));
    // No program at all is said at the last line read.
    RAWFRAME_EXPECT(refusedAt("spawn 1 a.b\n", WorldKestError::BadGameLine, "1"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x later f read a.b\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x simulation f read\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x simulation f read a.b\n", WorldKestError::UnknownName, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "spawn 0 a.b\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "spawn 1 x=1\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "prefab 0 a.scene\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "prefab 00000000000000a1 a.scene\nprefab 00000000000000a1 b.scene\n",
                  WorldKestError::BadGameLine,
                  "3"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "prefab 00000000000000a1 a.scene\nprefab 00000000000000a2 a.scene\n",
                  WorldKestError::BadGameLine,
                  "3"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "scene a.scene\nscene a.scene\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("# nothing\n", WorldKestError::BadGameLine, "1"));
}

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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{*plan, composition::HostServices{.clock = &clock, .scope = &root}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
        composition::Composition composition{
            *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-checkpoint-" + std::to_string(::getpid()));
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
        composition::Composition composition{
            *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
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
