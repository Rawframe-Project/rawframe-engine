// Persistent identity in a game: a scene's persistent entity is named the
// same in every World that spawns it, one without a scene identity cannot
// be, and a program asks for one with world.persist.

#include "game_harness.h"
#include "rawframe/scene/scene.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/test.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/layouts.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

constexpr std::string_view kProgram = "module keep\n\nimport rawframe.world\n\nstruct Door {\n    open: i32\n}\n\n"
                                      "fn swing(count: i32, doors: [Door], entities: [world.Entity]) {\n"
                                      "    let i = 0\n    while i < count {\n        if doors[i].open == 7 {\n"
                                      "            world.persist(entities[i])\n            doors[i].open = 8\n"
                                      "        }\n        i = i + 1\n    }\n}\n";

constexpr std::string_view kGame = "program keep.kest\ncomponent 4f1a9c26-8b3d-4e70-a5c2-1d9e7b3f6a08 keep.door Door\n"
                                   "system keep.swing simulation swing write keep.door entities\n";

const base::Bits128 kLevel{.high = 0x6c6576656c000000, .low = 1};

/// Every persistent identity in the World the game loaded into.
std::vector<world::Persistent> persistentIn() {
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(world::Persistent::kComponentTypeId);
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<world::Persistent> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            world::Persistent value;
            std::memcpy(static_cast<void*>(&value), chunk.columns[0] + (row * sizeof value), sizeof value);
            found.push_back(value);
        }
    });
    return found;
}

/// Starts the game in `directory`, runs `ticks` ticks, and gives what it
/// holds; refused where the start is.
result::Result<std::vector<world::Persistent>> play(const std::filesystem::path& directory, int ticks) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const auto kConfiguration = composition::Configuration::parse("kest.game = " + (directory / "keep.game").string() +
                                                                  "\nworld.tick_rate = 10\nworld.root_seed = 99\n");
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    RAWFRAME_TRY(composition.start());
    clock.advance(execution::MonotonicDuration::fromMilliseconds(100 * ticks));
    composition.runHostPhase(composition::HostPhase::RunWorlds,
                             composition::HostFrame{.iteration = 0, .now = clock.now()});
    std::vector<world::Persistent> found = persistentIn();
    composition.stop();
    simulation = nullptr;
    return found;
}

/// The level: a persistent door and a door that is not.
std::string level(const std::filesystem::path& directory) {
    auto files = world_kest::GameFiles::fromDirectory(directory / "keep.game");
    const auto kCompiled = files.has_value() ? files->compile("keep.kest") : std::unexpected{files.error().clone()};
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        return {};
    }
    const auto kGameRead = world_kest::parseGame(kGame);
    const world_kest::GameComponent kPersistent{.id = world::Persistent::kComponentTypeId,
                                                .name = std::string{world::Persistent::kComponentName},
                                                .kestType = "Persistent"};
    const std::uint64_t kPersistentMark = world_kest::componentLayout(*kGameRead, **kCompiled, kPersistent)->mark;
    const scene::Scene kLevelScene{
        .schema = {{.component = "keep.door", .mark = (*kCompiled)->layout("Door")->mark},
                   {.component = std::string{world::Persistent::kComponentName}, .mark = kPersistentMark}},
        .entities = {{.id = base::Bits128{.high = 9, .low = 1},
                      .components = {{.name = "keep.door", .fields = {}},
                                     {.name = std::string{world::Persistent::kComponentName}, .fields = {}}}},
                     {.id = base::Bits128{.high = 9, .low = 2}, .components = {{.name = "keep.door", .fields = {}}}}}};
    return *scene::writeScene(kLevelScene);
}

} // namespace

RAWFRAME_TEST(ASceneEntityIsNamedTheSameInEveryWorld) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-persistent-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "keep.kest", kProgram);
    writeText(kDirectory / "keep.game", std::string{kGame} + "scene level.scene\n");
    writeText(
        kDirectory / "level.scene",
        "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n  \"entities\": []\n}\n");
    const std::string kLevelText = level(kDirectory);
    writeText(kDirectory / "level.scene", kLevelText);

    // Without a scene identity there is nothing to name it from.
    const auto kNameless = play(kDirectory, 0);
    RAWFRAME_EXPECT(!kNameless.has_value());

    writeText(kDirectory / "level.scene.rfmeta",
              "{\n  \"schema\": 1,\n  \"resourceId\": \"6c6576656c0000000000000000000001\",\n"
              "  \"importer\": \"rawframe.scene\"\n}\n");
    const auto kFirst = play(kDirectory, 0);
    const auto kSecond = play(kDirectory, 0);
    RAWFRAME_EXPECT(kFirst.has_value() && kSecond.has_value());
    if (kFirst.has_value() && kSecond.has_value()) {
        // One persistent door, named from the level and its id, in both.
        const world::PersistentEntityId kExpected =
            world::persistentFromSource(kLevel, base::Bits128{.high = 9, .low = 1});
        RAWFRAME_EXPECT(kFirst->size() == 1 && (*kFirst)[0].id() == kExpected);
        RAWFRAME_EXPECT(kSecond->size() == 1 && (*kSecond)[0].id() == kExpected);
    }
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(AProgramMakesAnEntityPersistent) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-persist-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    writeText(kDirectory / "keep.kest", kProgram);
    writeText(kDirectory / "keep.game", std::string{kGame} + "spawn 1 keep.door open=7\nspawn 2 keep.door\n");
    // Three ticks: the first persists the door that asks, the rest leave it.
    const auto kHeld = play(kDirectory, 3);
    RAWFRAME_EXPECT(kHeld.has_value() && kHeld->size() == 1);
    if (kHeld.has_value() && kHeld->size() == 1) {
        // Named from the World's stream, as a World of the same seed draws.
        schema::RegistryBuilder builder;
        builder.add<world::Persistent>();
        world::World twin{*builder.freeze(), world::WorldSettings{.rootSeed = world::RootSeed{99}}};
        RAWFRAME_EXPECT((*kHeld)[0].id() == world::newPersistentId(twin));
    }
    std::filesystem::remove_all(kDirectory);
}
