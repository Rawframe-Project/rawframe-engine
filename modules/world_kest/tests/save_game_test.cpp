// A game's save end to end: a counter the program persists counts while
// the World runs, is kept when the World stops, and is back where it was
// when the World starts again; a save that does not read stops the start
// and is left as it was.

#include "game_harness.h"
#include "rawframe/test/test.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_runtime/registrar.h"

#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

constexpr std::string_view kProgram =
    "module tally\n\nimport rawframe.world\n\nstruct Count {\n    ticks: i32\n}\n\n"
    "fn count(n: i32, counts: [Count], entities: [world.Entity]) {\n    let i = 0\n    while i < n {\n"
    "        if counts[i].ticks == 0 {\n            world.persist(entities[i])\n        }\n"
    "        counts[i].ticks = counts[i].ticks + 1\n        i = i + 1\n    }\n}\n";

constexpr std::string_view kGame =
    "program tally.kest\ncomponent 3e9b1d74-6a28-4f05-9c83-2b7e5d1a0f96 tally.count Count\n"
    "system tally.count simulation count write tally.count entities\n"
    "save progress tally.count\n";

const std::array<composition::RegistrarEntry, 4> kSaving = {
    kWatched[0],
    kWatched[1],
    kWatched[2],
    composition::RegistrarEntry{"world_runtime.saves", &world_runtime::registerSaves, world_runtime::kScopes}};

/// Each persistent count: its identity and ticks.
std::vector<std::pair<world::PersistentEntityId, std::int32_t>> counts() {
    world::World& world = *simulation->world();
    const auto kPersistent = world.registry().find(world::Persistent::kComponentTypeId);
    const auto kCount =
        world.registry().find(schema::ComponentTypeId::fromText("3e9b1d74-6a28-4f05-9c83-2b7e5d1a0f96"));
    const std::array<world::ColumnTerm, 2> kTerms = {world::ColumnTerm{*kPersistent, world::Access::Read},
                                                     world::ColumnTerm{*kCount, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::pair<world::PersistentEntityId, std::int32_t>> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            world::Persistent name;
            std::int32_t ticks = 0;
            std::memcpy(static_cast<void*>(&name), chunk.columns[0] + (row * sizeof name), sizeof name);
            std::memcpy(&ticks, chunk.columns[1] + (row * sizeof ticks), sizeof ticks);
            found.emplace_back(name.id(), ticks);
        }
    });
    return found;
}

struct Run {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{
        .kind = execution::ExecutorKind::BlockingIo, .workers = 1, .clock = &clock, .emitter = {}}};
    std::optional<composition::Plan> plan;
    std::optional<composition::Configuration> configuration;
    std::optional<composition::Composition> composition;

    ~Run() {
        if (composition.has_value()) {
            composition->stop();
        }
        io.stop();
        simulation = nullptr;
    }

    std::string program{kProgram};

    std::string extra;

    result::Status start(const std::filesystem::path& directory, std::string_view spawn) {
        writeText(directory / "tally.kest", program);
        writeText(directory / "tally.game", std::string{kGame} + std::string{spawn});
        std::vector<composition::Problem> problems;
        auto made = composition::compose(
            composition::CompositionRequest{.registrars = kSaving,
                                            .shutdownBudget = execution::MonotonicDuration::fromSeconds(2)},
            problems);
        RAWFRAME_EXPECT(made.has_value());
        plan = std::move(*made);
        configuration = *composition::Configuration::parse(
            "kest.game = " + (directory / "tally.game").string() +
            "\nworld.tick_rate = 10\nsave.directory = " + (directory / "saves").string() +
            "\nsave.namespace = 7a11700000000000000000000000000a\nsave.every_seconds = 1\n" + extra);
        composition.emplace(*plan,
                            composition::HostServices{
                                .clock = &clock, .scope = &root, .blockingIo = &io, .configuration = &*configuration});
        return composition->start();
    }

    void ticks(int count) {
        clock.advance(execution::MonotonicDuration::fromMilliseconds(100 * count));
        composition->runHostPhase(composition::HostPhase::RunWorlds,
                                  composition::HostFrame{.iteration = 0, .now = clock.now()});
    }
};

} // namespace

RAWFRAME_TEST(AGamesSaveBringsItsStateBack) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-save-game-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    std::vector<std::pair<world::PersistentEntityId, std::int32_t>> before;
    {
        Run run;
        const auto kStarted = run.start(kDirectory, "spawn 1 tally.count\n");
        RAWFRAME_EXPECT(kStarted.has_value());
        if (!kStarted.has_value()) {
            return;
        }
        run.ticks(4);
        before = counts();
        RAWFRAME_EXPECT(before.size() == 1 && before[0].second == 4);
        // Stopping keeps it.
        run.composition->stop();
        run.composition.reset();
    }
    RAWFRAME_EXPECT(std::filesystem::exists(kDirectory / "saves" / "world.rfsave"));
    {
        // The same game, with nothing spawned: the save makes the counter,
        // with its identity and its ticks, and it counts on from there.
        Run run;
        const auto kStarted = run.start(kDirectory, "");
        RAWFRAME_EXPECT(kStarted.has_value());
        if (!kStarted.has_value()) {
            return;
        }
        RAWFRAME_EXPECT(counts() == before);
        run.ticks(2);
        const auto kAfter = counts();
        RAWFRAME_EXPECT(kAfter.size() == 1 && kAfter[0].first == before[0].first && kAfter[0].second == 6);
        // A second on, it is kept while the World runs, on the I/O worker.
        const std::string kKept = readText(kDirectory / "saves" / "world.rfsave");
        run.ticks(10);
        run.composition->runHostPhase(composition::HostPhase::Maintenance,
                                      composition::HostFrame{.iteration = 1, .now = run.clock.now()});
        bool rewritten = false;
        for (int wait = 0; wait < 2000 && !rewritten; ++wait) {
            rewritten = readText(kDirectory / "saves" / "world.rfsave") != kKept;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        RAWFRAME_EXPECT(rewritten);
    }
    // A save that does not read stops the start, and is left for the
    // operator.
    const std::filesystem::path kSave = kDirectory / "saves" / "world.rfsave";
    std::string bytes = readText(kSave);
    bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x10);
    writeText(kSave, bytes);
    {
        Run run;
        RAWFRAME_EXPECT(!run.start(kDirectory, "").has_value());
        run.composition.reset();
    }
    RAWFRAME_EXPECT(readText(kSave) == bytes);
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(ASaveOutlivesAChangeToItsComponent) {
    // The count kept as an i32, then the game widens it to an i64 and adds
    // a field: the save is migrated as it is read, the ticks carried over.
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-save-migrate-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    world::PersistentEntityId kept;
    {
        Run run;
        RAWFRAME_EXPECT(run.start(kDirectory, "spawn 1 tally.count\n").has_value());
        run.ticks(3);
        kept = counts().at(0).first;
        run.composition->stop();
        run.composition.reset();
    }
    std::string program{kProgram};
    program.replace(program.find("ticks: i32"), 10, "best: i32\n    ticks: i64");
    program.replace(program.find("counts[i].ticks + 1"), 19, "counts[i].ticks + i64(1)");
    {
        Run run;
        run.program = program;
        RAWFRAME_EXPECT(run.start(kDirectory, "").has_value());
        world::World& world = *simulation->world();
        const auto kCount =
            world.registry().find(schema::ComponentTypeId::fromText("3e9b1d74-6a28-4f05-9c83-2b7e5d1a0f96"));
        const auto kPersistent = world.registry().find(world::Persistent::kComponentTypeId);
        const std::array<world::ColumnTerm, 2> kTerms = {world::ColumnTerm{*kPersistent, world::Access::Read},
                                                         world::ColumnTerm{*kCount, world::Access::Read}};
        auto query = world::ColumnQuery::resolve(kTerms, world.registry());
        std::vector<std::tuple<world::PersistentEntityId, std::int32_t, std::int64_t>> found;
        query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                world::Persistent name;
                std::int32_t best = 0;
                std::int64_t ticks = 0;
                std::memcpy(static_cast<void*>(&name), chunk.columns[0] + (row * sizeof name), sizeof name);
                std::memcpy(&best, chunk.columns[1] + (row * 16), sizeof best);
                std::memcpy(&ticks, chunk.columns[1] + (row * 16) + 8, sizeof ticks);
                found.emplace_back(name.id(), best, ticks);
            }
        });
        RAWFRAME_EXPECT(found.size() == 1 && std::get<0>(found[0]) == kept && std::get<1>(found[0]) == 0 &&
                        std::get<2>(found[0]) == 3);
    }
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(ACheckpointRestoreIsTheWorldItsSaveIsNot) {
    // A checkpoint at tick 2, a save at tick 5: restored from the
    // checkpoint, the World counts from 2, and the save is not applied over
    // it.
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-save-restore-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    {
        Run run;
        run.extra = "checkpoint.capture_ticks = 2\ncheckpoint.capture_prefix = " + (kDirectory / "at").string() + "\n";
        RAWFRAME_EXPECT(run.start(kDirectory, "spawn 1 tally.count\n").has_value());
        run.ticks(2);
        run.composition->runHostPhase(composition::HostPhase::Maintenance,
                                      composition::HostFrame{.iteration = 0, .now = run.clock.now()});
        run.ticks(3);
        RAWFRAME_EXPECT(counts().size() == 1 && counts()[0].second == 5);
        run.composition->stop();
        run.composition.reset();
    }
    RAWFRAME_EXPECT(std::filesystem::exists(kDirectory / "at2.rfsn"));
    {
        Run run;
        run.extra = "checkpoint.restore = " + (kDirectory / "at2.rfsn").string() + "\n";
        RAWFRAME_EXPECT(run.start(kDirectory, "spawn 1 tally.count\n").has_value());
        RAWFRAME_EXPECT(counts().size() == 1 && counts()[0].second == 2);
    }
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(ASaveLineIsParsedAndChecked) {
    const auto kParsed = world_kest::parseGame(kGame);
    RAWFRAME_EXPECT(kParsed.has_value() && kParsed->save.document == "progress" &&
                    kParsed->save.components == std::vector<std::string>{"tally.count"});
    constexpr std::string_view kBase = "program p.kest\ncomponent 3e9b1d74-6a28-4f05-9c83-2b7e5d1a0f96 t.c C\n";
    // Twice, with no component, one twice, and one the game lacks.
    for (const std::string_view kLine : {"save a t.c\nsave b t.c\n", "save a\n", "save a t.c t.c\n", "save a t.x\n"}) {
        RAWFRAME_EXPECT(!world_kest::parseGame(std::string{kBase} + std::string{kLine}).has_value());
    }
}
