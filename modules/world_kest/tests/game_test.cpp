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

#include <array>
#include <string>
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
                          "system a.move simulation integrate write a.position read a.velocity after a.input\n"
                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position  # later is fine\n"
                          "component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 a.velocity Velocity\r\n"
                          "spawn 2 a.position x=1 a.velocity\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->program == "movers.kest");
    RAWFRAME_EXPECT(game->components.size() == 2 && game->components[1].kestType == "Velocity");
    RAWFRAME_EXPECT(game->systems.size() == 1 && game->systems[0].columns.size() == 2);
    RAWFRAME_EXPECT(game->systems[0].columns[0].access == world::Access::Write);
    RAWFRAME_EXPECT((game->systems[0].after == std::vector<std::string>{"a.input"}));
    RAWFRAME_EXPECT(game->spawns.size() == 1 && game->spawns[0].count == 2);
    RAWFRAME_EXPECT(game->spawns[0].components.size() == 2 && game->spawns[0].components[0].fields.size() == 1);
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
                                     std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + "broken.game\n"}) {
        const auto kConfiguration = composition::Configuration::parse(kText);
        composition::Composition composition{
            *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
        RAWFRAME_EXPECT(!composition.start().has_value());
    }
}
