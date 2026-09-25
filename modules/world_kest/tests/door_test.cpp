// Hostile calls into the engine's doors (the rules' fuzzing duty for
// Kest-facing doors): a program passes each physics query NaN, the
// infinities, the largest and smallest numbers, negative sizes, classes and
// indices past any there are, and moments far from any kept, argument by
// argument and all at once; and makes, marks, and destroys entities twice
// over. Every call answers, the World keeps stepping, and the sanitizers
// of the full check find nothing.

#include "game_harness.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"

#include <cstring>
#include <string>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

const std::array<composition::RegistrarEntry, 5> kWithPhysics = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics2d", &physics2d::registerParticipants, physics2d::kScopes},
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

/// Plays the game `name` beside the test's games for `ticks` ticks at 60 Hz
/// and hands its World to `check` before it stops.
template <typename Check> void play(std::string_view name, std::uint64_t ticks, const Check& check) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = std::string{"kest.game = "} + RAWFRAME_WORLD_KEST_GAMES + std::string{name} + "\n" +
                              "world.tick_rate = 60\n" + "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
    auto started = composition.start();
    if (!started.has_value()) {
        std::fprintf(stderr, "%s\n", std::string{started.error().description()}.c_str());
        for (const auto& field : started.error().context()) {
            std::fprintf(stderr, "  %s: %s\n", std::string{field.key}.c_str(), std::string{field.value}.c_str());
        }
    }
    RAWFRAME_EXPECT(started.has_value());
    if (!started.has_value()) {
        return;
    }
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(composition::HostPhase::RunWorlds,
                                 composition::HostFrame{.iteration = tick, .now = clock.now()});
    }
    check(*simulation->world());
    composition.stop();
    simulation = nullptr;
}

/// The calls the poke made on its last run.
std::uint32_t callsMade(world::World& world) {
    const auto kPoke = world.registry().find(schema::ComponentTypeId::fromText("13fbb802-8fea-4b06-a6c7-01d749ff4b0e"));
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kPoke, world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::uint32_t calls = 0;
    query->forEachChunk(world, [&calls](const world::ColumnChunk& chunk) {
        if (!chunk.entities.empty()) {
            std::memcpy(&calls, chunk.columns[0], sizeof calls);
        }
    });
    return calls;
}

} // namespace

RAWFRAME_TEST(EveryThreeDimensionalQueryAnswersHostileValues) {
    play("hostile3d.game", 30, [](world::World& world) {
        // Ten kinds of value, in each of seven arguments and in all at once,
        // through six doors.
        RAWFRAME_EXPECT(callsMade(world) == 10 * 8 * 6);
        auto bodies =
            *world::Query<world::Read<physics3d::Body3D>, world::Read<physics3d::Pose3D>>::resolve(world.registry());
        bodies.forEach(world, [](world::EntityHandle, const physics3d::Body3D& body, const physics3d::Pose3D& pose) {
            RAWFRAME_EXPECT(body.motion != 2 || pose.y < 3);
        });
    });
}

RAWFRAME_TEST(EveryTwoDimensionalQueryAnswersHostileValues) {
    play("hostile2d.game", 30, [](world::World& world) {
        RAWFRAME_EXPECT(callsMade(world) == 10 * 6 * 6);
        auto bodies =
            *world::Query<world::Read<physics2d::Body2D>, world::Read<physics2d::Pose2D>>::resolve(world.registry());
        bodies.forEach(world, [](world::EntityHandle, const physics2d::Body2D& body, const physics2d::Pose2D& pose) {
            RAWFRAME_EXPECT(body.motion != 2 || pose.y < 3);
        });
    });
}

RAWFRAME_TEST(StructuralDoorsTwiceOverAreSkippedAtTheBarrier) {
    play("churn.game", 3, [](world::World& world) {
        // The four marked entities are gone, their second destroy and the
        // mark inserted on them skipped; each run's new entity keeps no mark.
        const auto kMark =
            world.registry().find(schema::ComponentTypeId::fromText("f4faaf4f-ceb6-4c26-b31c-6740919259a9"));
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kMark, world::Access::Read}};
        auto marks = world::ColumnQuery::resolve(kTerms, world.registry());
        std::size_t marked = 0;
        marks->forEachChunk(world, [&marked](const world::ColumnChunk& chunk) {
            marked += chunk.entities.size();
        });
        RAWFRAME_EXPECT(marked == 0 && world.entityCount() == 4);
    });
}
