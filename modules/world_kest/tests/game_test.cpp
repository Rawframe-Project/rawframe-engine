// Game descriptions: what parses, what is refused and where, and a whole
// Kest-scripted game loaded through composition and ticked by the World.

#include "rawframe/composition/composition.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace rawframe;
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
    RAWFRAME_EXPECT(game->components.size() == 2 && game->components[1].kestType == "Velocity");
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
    RAWFRAME_EXPECT(refusedAt("# nothing\n", WorldKestError::BadGameLine, "1"));
}

namespace {

const std::array<composition::RegistrarEntry, 2> kRegistrars = {
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
};

/// The simulation the game was loaded into, found through a participant of
/// the test's own.
world_runtime::Simulation* simulation = nullptr;

result::Result<composition::ParticipantOwner> makeWatcher(composition::ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(simulation, context.capability(world_runtime::kSimulation));
    struct Watcher final : composition::Participant {};
    return composition::ParticipantOwner{new Watcher{}};
}

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};

void registerWatcher(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.watcher",
        .factory = &makeWatcher,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
    });
}

const std::array<composition::RegistrarEntry, 3> kWatched = {
    kRegistrars[0], kRegistrars[1], composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

std::vector<std::pair<float, float>> positions() {
    world::World& world = *simulation->world();
    const auto kId = world.registry().find(schema::ComponentTypeId::fromText("0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<std::pair<float, float>> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        const auto* values = reinterpret_cast<const float*>(chunk.columns[0]);
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            found.emplace_back(values[row * 2], values[(row * 2) + 1]);
        }
    });
    return found;
}

} // namespace

RAWFRAME_TEST(AKestGameRunsInTheWorld) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWatched,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(plan.has_value());
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "movers.game\n" +
                              "kest.library = " + RAWFRAME_KEST_LIBRARY + "\n" + "world.tick_rate = 10\n" +
                              "world.maximum_ticks_per_iteration = 100\n";
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
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES +
                                         "mispredicted.game\nkest.library = " + RAWFRAME_KEST_LIBRARY + "\n",
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES +
                                         "misinterested.game\nkest.library = " + RAWFRAME_KEST_LIBRARY + "\n"}) {
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
                              "kest.library = " + RAWFRAME_KEST_LIBRARY + "\n" + "world.tick_rate = 10\n" +
                              "world.maximum_ticks_per_iteration = 1\n";
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

void writeText(const std::filesystem::path& path, std::string_view text) {
    if (std::FILE* file = std::fopen(path.string().c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }
}

std::string readText(const std::filesystem::path& path) {
    std::string text;
    if (std::FILE* file = std::fopen(path.string().c_str(), "rb")) {
        char chunk[4096];
        std::size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof chunk, file)) != 0) {
            text.append(chunk, got);
        }
        std::fclose(file);
    }
    return text;
}

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
    const std::string kText = "kest.game = " + (kDirectory / "movers.game").string() +
                              "\nkest.library = " + RAWFRAME_KEST_LIBRARY +
                              "\nkest.reload_every = 1\nworld.tick_rate = 10\n";
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
                              "linked.game\nkest.library = " + RAWFRAME_KEST_LIBRARY +
                              "\nworld.tick_rate = 10\nworld.maximum_ticks_per_iteration = 100\n";
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
