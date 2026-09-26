// Games that start from scenes (D92 to D94): a scene's entities spawned
// beside spawn lines, checked against the program's layouts; references
// between them; and a game's spawn lines written as a scene.

#include "game_harness.h"
#include "rawframe/scene/scene.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/spawn_scene.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

/// A scene of nothing, standing in while a test reads the program's marks.
constexpr std::string_view kEmptyScene =
    "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n  \"entities\": []\n}\n";

} // namespace

RAWFRAME_TEST(AGameStartsWithItsScenes) {
    // Movers with their starting entities in a scene, not spawn lines: two
    // at rest where the scene puts them, one moving, and one spawn line.
    const std::filesystem::path kDirectory = test::scratchDirectory("scene");
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "movers.kest", readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.kest"));
    std::string game = readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.game");
    game = game.substr(0, game.find("spawn 3"));
    game += "spawn 1 movers.position x=1 y=1 movers.velocity\nscene start.scene\n";
    writeText(kDirectory / "movers.game", game);

    // The marks the scene is authored against: the program's layouts.
    writeText(kDirectory / "start.scene", kEmptyScene);
    auto files = world_kest::GameFiles::fromDirectory(kDirectory / "movers.game");
    RAWFRAME_EXPECT(files.has_value());
    const auto kProgram = files.has_value() ? files->compile("movers.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        return;
    }
    const std::uint64_t kPosition = (*kProgram)->layout("Position")->mark;
    const std::uint64_t kVelocity = (*kProgram)->layout("Velocity")->mark;
    const auto kNumber = [](std::string text) {
        return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::move(text)};
    };
    scene::Scene start{
        .schema = {{.component = "movers.position", .mark = kPosition},
                   {.component = "movers.velocity", .mark = kVelocity}},
        .entities = {
            {.id = base::Bits128{.high = 0, .low = 1},
             .name = "resting",
             .components = {{.name = "movers.position", .fields = {{.name = "x", .value = kNumber("-5")}}},
                            {.name = "movers.velocity", .fields = {}}}},
            {.id = base::Bits128{.high = 0, .low = 2},
             .components = {{.name = "movers.position", .fields = {{.name = "y", .value = kNumber("7.5")}}},
                            {.name = "movers.velocity", .fields = {{.name = "dx", .value = kNumber("2")}}}}},
        }};
    const auto kRun =
        [&kDirectory](const scene::Scene& written) -> std::optional<std::vector<std::pair<float, float>>> {
        const auto kText = scene::writeScene(written);
        RAWFRAME_EXPECT(kText.has_value());
        if (!kText.has_value()) {
            return std::nullopt;
        }
        writeText(kDirectory / "start.scene", *kText);
        std::vector<composition::Problem> problems;
        auto plan = composition::compose(
            composition::CompositionRequest{.registrars = kWatched,
                                            .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
            problems);
        const auto kConfiguration =
            composition::Configuration::parse("kest.game = " + (kDirectory / "movers.game").string() +
                                              "\nworld.tick_rate = 10\nworld.maximum_ticks_per_iteration = 100\n");
        execution::ManualClock clock;
        execution::CancellationScope root{clock};
        composition::Composition composition{*plan,
                                             composition::HostServices{.clock = &clock,
                                                                       .scope = &root,
                                                                       .cpu = &test::cpuExecutor(),
                                                                       .blockingIo = &test::blockingIoExecutor(),
                                                                       .configuration = &*kConfiguration}};
        if (!composition.start().has_value()) {
            simulation = nullptr;
            return std::nullopt;
        }
        clock.advance(execution::MonotonicDuration::fromSeconds(1));
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = 0, .now = clock.now()});
        auto found = positions();
        composition.stop();
        simulation = nullptr;
        std::ranges::sort(found);
        return found;
    };
    const auto kFound = kRun(start);
    RAWFRAME_EXPECT(kFound.has_value());
    if (kFound.has_value()) {
        // Ten ticks: the spawn line's and the resting one stay, the moving
        // one goes twenty meters.
        const std::vector<std::pair<float, float>> kExpected = {{-5.0F, 0.0F}, {1.0F, 1.0F}, {20.0F, 7.5F}};
        RAWFRAME_EXPECT(*kFound == kExpected);
    }
    // Authored against another layout, or naming another component: the
    // game does not start.
    scene::Scene stale = start;
    stale.schema[0].mark ^= 1U;
    RAWFRAME_EXPECT(!kRun(stale).has_value());
    scene::Scene stranger = start;
    stranger.schema.insert(stranger.schema.begin() + 1, {.component = "movers.spin", .mark = 1});
    stranger.entities[0].components.insert(stranger.entities[0].components.begin() + 1,
                                           {.name = "movers.spin", .fields = {}});
    RAWFRAME_EXPECT(!kRun(stranger).has_value());
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AScenesEntitiesNameEachOther) {
    // Two links naming each other and one naming none, by their ids in the
    // scene; spawned, each holds the other's entity.
    const std::filesystem::path kDirectory = test::scratchDirectory("links");
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "linked.kest", readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "linked.kest"));
    const std::string kGame = "program linked.kest\ncomponent 5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84 linked.link Link\n";
    writeText(kDirectory / "linked.game", kGame + "entity linked.link next\nscene links.scene\n");
    writeText(kDirectory / "links.scene", kEmptyScene);
    auto files = world_kest::GameFiles::fromDirectory(kDirectory / "linked.game");
    const auto kProgram = files.has_value() ? files->compile("linked.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        return;
    }
    const base::Bits128 kFirst{.high = 0, .low = 1};
    const base::Bits128 kSecond{.high = 0, .low = 2};
    const auto kLink = [](base::Bits128 to) {
        return scene::SceneField{.name = "next", .value = {.kind = scene::FieldValue::Kind::Entity, .entity = to}};
    };
    const scene::Scene kLinks{
        .schema = {{.component = "linked.link", .mark = (*kProgram)->layout("Link")->mark}},
        .entities = {
            {.id = kFirst,
             .components = {{.name = "linked.link",
                             .fields = {{.name = "hops", .value = {.number = "1"}}, kLink(kSecond)}}}},
            {.id = kSecond,
             .components = {{.name = "linked.link",
                             .fields = {{.name = "hops", .value = {.number = "2"}}, kLink(kFirst)}}}},
            {.id = base::Bits128{.high = 0, .low = 3},
             .components = {{.name = "linked.link", .fields = {{.name = "hops", .value = {.number = "3"}}}}}},
        }};
    const auto kStart = [&kDirectory](const scene::Scene& authored, std::string_view game) {
        writeText(kDirectory / "links.scene", *scene::writeScene(authored));
        writeText(kDirectory / "linked.game", game);
        std::vector<composition::Problem> problems;
        auto plan = composition::compose(
            composition::CompositionRequest{.registrars = kWatched,
                                            .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
            problems);
        const auto kConfiguration =
            composition::Configuration::parse("kest.game = " + (kDirectory / "linked.game").string() + "\n");
        execution::ManualClock clock;
        execution::CancellationScope root{clock};
        composition::Composition composition{*plan,
                                             composition::HostServices{.clock = &clock,
                                                                       .scope = &root,
                                                                       .cpu = &test::cpuExecutor(),
                                                                       .blockingIo = &test::blockingIoExecutor(),
                                                                       .configuration = &*kConfiguration}};
        // By hops: each link's entity and the entity it holds.
        std::vector<std::tuple<std::int32_t, world::EntityHandle, world::EntityHandle>> links;
        if (composition.start().has_value()) {
            world::World& world = *simulation->world();
            const auto kId =
                world.registry().find(schema::ComponentTypeId::fromText("5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84"));
            const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
            auto query = world::ColumnQuery::resolve(kTerms, world.registry());
            query->forEachChunk(world, [&links](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    std::array<std::uint32_t, 3> link{};
                    std::memcpy(link.data(), chunk.columns[0] + (row * 12), 12);
                    links.emplace_back(static_cast<std::int32_t>(link[2]),
                                       chunk.entities[row],
                                       world::EntityHandle{.slot = link[0], .generation = link[1]});
                }
            });
            composition.stop();
        }
        simulation = nullptr;
        std::ranges::sort(links, {}, [](const auto& link) {
            return std::get<0>(link);
        });
        return links;
    };
    const auto kLinked = kStart(kLinks, kGame + "entity linked.link next\nscene links.scene\n");
    RAWFRAME_EXPECT(kLinked.size() == 3);
    if (kLinked.size() == 3) {
        RAWFRAME_EXPECT(std::get<2>(kLinked[0]) == std::get<1>(kLinked[1]) &&
                        std::get<2>(kLinked[1]) == std::get<1>(kLinked[0]) && std::get<2>(kLinked[2]).isNull());
    }
    // A reference only through a field the description declares holds one.
    RAWFRAME_EXPECT(kStart(kLinks, kGame + "scene links.scene\n").empty());
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(SpawnLinesBecomeAScene) {
    auto files = world_kest::GameFiles::fromDirectory(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.game");
    const auto kProgram = files.has_value() ? files->compile("movers.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        return;
    }
    std::uint64_t next = 0;
    const auto kScene = world_kest::spawnsAsScene(files->description(), **kProgram, [&next] {
        return base::Bits128{.high = 0, .low = ++next};
    });
    RAWFRAME_EXPECT(kScene.has_value());
    if (!kScene.has_value()) {
        return;
    }
    // Four entities, one for each spawned; each component's mark the
    // program's; a field at nought left out; the form holds.
    const base::Bits128 kFourth{.high = 0, .low = 4};
    RAWFRAME_EXPECT(kScene->entities.size() == 4 && kScene->entities[3].id == kFourth);
    RAWFRAME_EXPECT(kScene->schema.size() == 2 && kScene->schema[0].mark == (*kProgram)->layout("Position")->mark &&
                    kScene->schema[1].mark == (*kProgram)->layout("Velocity")->mark);
    RAWFRAME_EXPECT(kScene->entities[0].components[0].fields.empty() &&
                    kScene->entities[0].components[1].fields.size() == 2 &&
                    kScene->entities[3].components[0].fields.size() == 1 &&
                    kScene->entities[3].components[0].fields[0].value.number == "99.5");
    RAWFRAME_EXPECT(scene::writeScene(*kScene).has_value());
}

RAWFRAME_TEST(AGameResolvesItsScenesInstances) {
    // Movers whose start scene instances a two-mover prefab twice, found by
    // its sidecar: one copy as authored, one moved and slowed.
    const std::filesystem::path kDirectory = test::scratchDirectory("instances");
    std::filesystem::create_directories(kDirectory / "prefabs");
    writeText(kDirectory / "movers.kest", readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.kest"));
    std::string game = readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "movers.game");
    writeText(kDirectory / "movers.game", game.substr(0, game.find("spawn 3")) + "scene start.scene\n");
    writeText(kDirectory / "start.scene", kEmptyScene);
    auto files = world_kest::GameFiles::fromDirectory(kDirectory / "movers.game");
    const auto kProgram = files.has_value() ? files->compile("movers.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        return;
    }
    const std::vector<scene::SchemaMark> kSchema = {
        {.component = "movers.position", .mark = (*kProgram)->layout("Position")->mark},
        {.component = "movers.velocity", .mark = (*kProgram)->layout("Velocity")->mark}};
    const auto kNumber = [](std::string text) {
        return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::move(text)};
    };
    const auto kId = [](std::uint64_t low) {
        return base::Bits128{.high = 0x2000, .low = low};
    };
    const scene::Scene kPair{
        .schema = kSchema,
        .entities = {{.id = kId(1),
                      .components = {{.name = "movers.position", .fields = {{.name = "x", .value = kNumber("1")}}},
                                     {.name = "movers.velocity", .fields = {{.name = "dx", .value = kNumber("1")}}}}},
                     {.id = kId(2),
                      .components = {{.name = "movers.position", .fields = {{.name = "x", .value = kNumber("2")}}},
                                     {.name = "movers.velocity", .fields = {}}}}}};
    const base::Bits128 kPairScene = base::parseBits128Hex("6a0e3f1c2b4d5e6f7a8b9c0d1e2f3a4b").value;
    writeText(kDirectory / "prefabs" / "pair.scene", *scene::writeScene(kPair));
    writeText(kDirectory / "prefabs" / "pair.scene.rfmeta",
              "{\n  \"schema\": 1,\n  \"resourceId\": \"6a0e3f1c2b4d5e6f7a8b9c0d1e2f3a4b\",\n  \"importer\": "
              "\"rawframe.scene\"\n}\n");
    scene::Scene start{.schema = kSchema, .entities = {}};
    start.instances.push_back(
        {.scene = kPairScene,
         .entities = {{.source = kId(1), .instance = kId(11)}, {.source = kId(2), .instance = kId(12)}},
         .overrides = {}});
    start.instances.push_back(
        {.scene = kPairScene,
         .entities = {{.source = kId(1), .instance = kId(21)}, {.source = kId(2), .instance = kId(22)}},
         .overrides = {{.entity = kId(21),
                        .component = "movers.position",
                        .kind = scene::Override::Kind::Set,
                        .fields = {{.name = "y", .value = kNumber("50")}}},
                       {.entity = kId(21),
                        .component = "movers.velocity",
                        .kind = scene::Override::Kind::Set,
                        .fields = {{.name = "dx", .value = kNumber("0")}}}}});
    writeText(kDirectory / "start.scene", *scene::writeScene(start));

    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const auto kConfiguration = composition::Configuration::parse(
        "kest.game = " + (kDirectory / "movers.game").string() + "\nworld.tick_rate = 10\n");
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    RAWFRAME_EXPECT(composition.start().has_value());
    if (simulation != nullptr) {
        auto found = positions();
        std::ranges::sort(found);
        // Both copies' movers; the second copy's first moved up, and at
        // rest.
        const std::vector<std::pair<float, float>> kExpected = {
            {1.0F, 0.0F}, {1.0F, 50.0F}, {2.0F, 0.0F}, {2.0F, 0.0F}};
        RAWFRAME_EXPECT(found == kExpected);
        composition.stop();
    }
    simulation = nullptr;

    // Without the prefab's sidecar the instance names nothing, and the game
    // does not load.
    std::filesystem::remove(kDirectory / "prefabs" / "pair.scene.rfmeta");
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromDirectory(kDirectory / "movers.game").has_value());
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AProgramLinksEntitiesItCreatesInOneRun) {
    // A seed plants a pair of links naming each other, both created in the
    // same run: each names the entity the barrier made for the other.
    const std::filesystem::path kDirectory = test::scratchDirectory("pairs");
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "pairs.kest",
              "module pairs\n\nimport rawframe.world\n\nstruct Seed {\n    planted: i32\n}\n\nstruct Link {\n"
              "    next: world.Entity\n    hops: i32\n}\n\nextern fn Link.insert(entity: world.Entity, value: Link)\n\n"
              "fn plant(count: i32, seeds: [Seed]) {\n    let i = 0\n    while i < count {\n"
              "        if seeds[i].planted == 0 {\n            let a = world.create()\n"
              "            let b = world.create()\n            Link.insert(a, Link(b, 1))\n"
              "            Link.insert(b, Link(a, 2))\n            seeds[i].planted = 1\n        }\n"
              "        i = i + 1\n    }\n}\n");
    writeText(kDirectory / "pairs.game",
              "program pairs.kest\ncomponent 6d2e8f14-3b7a-4c95-a1e0-9f5c7b3d2a86 pairs.seed Seed\n"
              "component 5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84 pairs.link Link\nentity pairs.link next\n"
              "system pairs.plant simulation plant write pairs.seed\nspawn 1 pairs.seed\n");
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const auto kConfiguration =
        composition::Configuration::parse("kest.game = " + (kDirectory / "pairs.game").string() +
                                          "\nworld.tick_rate = 10\nworld.maximum_ticks_per_iteration = 100\n");
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    const auto kStarted = composition.start();
    RAWFRAME_EXPECT(kStarted.has_value());
    if (!kStarted.has_value()) {
        return;
    }
    clock.advance(execution::MonotonicDuration::fromMilliseconds(300));
    composition.runHostPhase(composition::HostPhase::RunWorlds,
                             composition::HostFrame{.iteration = 0, .now = clock.now()});
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::pair<world::EntityHandle, world::EntityHandle>> links;
    query->forEachChunk(world, [&links](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            world::EntityHandle next;
            std::memcpy(&next, chunk.columns[0] + (row * 12), sizeof next);
            links.emplace_back(chunk.entities[row], next);
        }
    });
    // One pair, planted once, each naming the other.
    RAWFRAME_EXPECT(links.size() == 2);
    if (links.size() == 2) {
        RAWFRAME_EXPECT(links[0].second == links[1].first && links[1].second == links[0].first);
    }
    composition.stop();
    simulation = nullptr;
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AProgramSpawnsAPrefabWhole) {
    // A seed spawns a prefab of two links naming each other, once a tick
    // for two ticks: two pairs, each pair's links naming each other.
    const std::filesystem::path kDirectory = test::scratchDirectory("prefab");
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "spawner.kest",
              "module spawner\n\nimport rawframe.world\nimport rawframe.scene\n\nstruct Seed {\n    planted: i32\n}\n\n"
              "struct Link {\n    next: world.Entity\n    hops: i32\n}\n\nconst PAIR: u64 = 0x5eed000000000001\n\n"
              "fn plant(count: i32, seeds: [Seed]) {\n    let i = 0\n    while i < count {\n"
              "        if seeds[i].planted < 2 {\n            let first = scene.spawn(PAIR)\n"
              "            seeds[i].planted = seeds[i].planted + 1\n        }\n        i = i + 1\n    }\n}\n\n"
              "fn hold(count: i32, links: [Link]) {\n}\n");
    const std::string kGame =
        "program spawner.kest\ncomponent 6d2e8f14-3b7a-4c95-a1e0-9f5c7b3d2a86 spawner.seed Seed\n"
        "component 5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84 spawner.link Link\nentity spawner.link next\n"
        "system spawner.plant simulation plant write spawner.seed\nspawn 1 spawner.seed\n";
    writeText(kDirectory / "spawner.game", kGame + "prefab 5eed000000000001 pair.scene\n");
    writeText(kDirectory / "pair.scene", kEmptyScene);
    auto files = world_kest::GameFiles::fromDirectory(kDirectory / "spawner.game");
    const auto kProgram = files.has_value() ? files->compile("spawner.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        return;
    }
    const auto kLink = [](std::uint64_t to, std::string hops) {
        return scene::SceneComponent{.name = "spawner.link",
                                     .fields = {{.name = "hops", .value = {.number = std::move(hops)}},
                                                {.name = "next",
                                                 .value = {.kind = scene::FieldValue::Kind::Entity,
                                                           .entity = base::Bits128{.high = 3, .low = to}}}}};
    };
    const scene::Scene kPair{.schema = {{.component = "spawner.link", .mark = (*kProgram)->layout("Link")->mark}},
                             .entities = {{.id = base::Bits128{.high = 3, .low = 1}, .components = {kLink(2, "1")}},
                                          {.id = base::Bits128{.high = 3, .low = 2}, .components = {kLink(1, "2")}}}};
    writeText(kDirectory / "pair.scene", *scene::writeScene(kPair));

    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const auto kConfiguration =
        composition::Configuration::parse("kest.game = " + (kDirectory / "spawner.game").string() +
                                          "\nworld.tick_rate = 10\nworld.maximum_ticks_per_iteration = 100\n");
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    const auto kStarted = composition.start();
    RAWFRAME_EXPECT(kStarted.has_value());
    if (!kStarted.has_value()) {
        return;
    }
    clock.advance(execution::MonotonicDuration::fromMilliseconds(500));
    composition.runHostPhase(composition::HostPhase::RunWorlds,
                             composition::HostFrame{.iteration = 0, .now = clock.now()});
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::tuple<world::EntityHandle, world::EntityHandle, std::int32_t>> links;
    query->forEachChunk(world, [&links](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            world::EntityHandle next;
            std::int32_t hops = 0;
            std::memcpy(&next, chunk.columns[0] + (row * 12), sizeof next);
            std::memcpy(&hops, chunk.columns[0] + (row * 12) + 8, sizeof hops);
            links.emplace_back(chunk.entities[row], next, hops);
        }
    });
    RAWFRAME_EXPECT(links.size() == 4);
    for (const auto& [kEntity, kNext, kHops] : links) {
        // The one it names names it back, with the other count of hops.
        const auto kOther = std::ranges::find(links, kNext, [](const auto& link) {
            return std::get<0>(link);
        });
        RAWFRAME_EXPECT(kOther != links.end() && std::get<1>(*kOther) == kEntity && std::get<2>(*kOther) + kHops == 3);
    }
    composition.stop();
    simulation = nullptr;

    // A prefab of an identity the game does not declare stops the system.
    writeText(kDirectory / "spawner.game", kGame + "prefab 5eed000000000002 pair.scene\n");
    std::vector<composition::Problem> again;
    auto unknown = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        again);
    execution::ManualClock later;
    execution::CancellationScope laterRoot{later};
    composition::Composition refused{*unknown,
                                     composition::HostServices{.clock = &later,
                                                               .scope = &laterRoot,
                                                               .cpu = &test::cpuExecutor(),
                                                               .blockingIo = &test::blockingIoExecutor(),
                                                               .configuration = &*kConfiguration}};
    if (refused.start().has_value()) {
        later.advance(execution::MonotonicDuration::fromMilliseconds(100));
        refused.runHostPhase(composition::HostPhase::RunWorlds,
                             composition::HostFrame{.iteration = 0, .now = later.now()});
        world::World& unchanged = *simulation->world();
        RAWFRAME_EXPECT(unchanged.entityCount() == 1);
        refused.stop();
    }
    simulation = nullptr;
    std::filesystem::remove_all(kDirectory);
}
