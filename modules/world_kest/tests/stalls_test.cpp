// Stalls' levels (D391), played: an owner builds a stall, raises it a level
// with another press, and up to the highest level and no further; another
// owner pressing on that stall raises nothing. The test moves the owners'
// hands between ticks, as their clients' input would.

#include "game_harness.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

const std::array<composition::RegistrarEntry, 4> kWithPhysics = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

constexpr auto kHandId = schema::ComponentTypeId::fromText("0097c04f-e081-44c5-b93d-ba7a6836d3e8");
constexpr auto kOwnerId = schema::ComponentTypeId::fromText("4143f760-31c1-469d-bda7-7946878bcaba");
constexpr auto kStallId = schema::ComponentTypeId::fromText("20f1dacf-e8f6-4c1f-be56-5fc36f9ade8e");

/// Stalls' Hand, Owner, and Stall (tycoon.kest), as the program lays them
/// out.
struct Hand {
    float moveX = 0;
    float moveZ = 0;
    float pointed = 0;
    float pointX = 0;
    float pointZ = 0;
    float build = 0;
};

struct Owner {
    std::uint32_t joined = 0;
    double cursorX = 0;
    double cursorZ = 0;
    std::int64_t coins = 0;
    std::uint32_t stalls = 0;
    std::uint32_t levels = 0;
    std::uint32_t held = 0;
    std::uint32_t clock = 0;
};

struct Stall {
    world::EntityHandle owner;
    std::int32_t plotX = 0;
    std::int32_t plotZ = 0;
    std::uint32_t level = 0;
};

/// Every entity with component `id`, in World order.
std::vector<world::EntityHandle> holding(world::World& world, schema::ComponentTypeId id) {
    const std::array<world::ColumnTerm, 1> kTerms = {
        world::ColumnTerm{*world.registry().find(id), world::Access::Read}};
    auto query = world::ColumnQuery::resolve(kTerms, world.registry());
    std::vector<world::EntityHandle> found;
    query->forEachChunk(world, [&found](const world::ColumnChunk& chunk) {
        found.insert(found.end(), chunk.entities.begin(), chunk.entities.end());
    });
    return found;
}

template <typename T> T* valueOf(world::World& world, world::EntityHandle entity, schema::ComponentTypeId id) {
    return static_cast<T*>(world.getErased(entity, *world.registry().find(id)));
}

} // namespace

RAWFRAME_TEST(AnOwnerRaisesItsOwnStallsAndNoOneElses) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = "kest.game = " + std::string{RAWFRAME_SAMPLE_GAMES} + "tycoon/levels.game\n" +
                              "world.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
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
    std::uint64_t iteration = 0;
    const auto kRun = [&](std::uint64_t ticks) {
        for (std::uint64_t tick = 0; tick < ticks; ++tick) {
            clock.advance(execution::MonotonicDuration{16'666'667});
            composition.runHostPhase(composition::HostPhase::RunWorlds,
                                     composition::HostFrame{.iteration = iteration++, .now = clock.now()});
        }
    };
    world::World& world = *simulation->world();

    // The first owner builds on its first tick and has spent the cost.
    kRun(3);
    std::vector<world::EntityHandle> owners = holding(world, kOwnerId);
    RAWFRAME_EXPECT(owners.size() == 2);
    if (owners.size() != 2) {
        composition.stop();
        return;
    }
    // The builder is the one whose cursor is on the corner plot.
    if (valueOf<Owner>(world, owners[0], kOwnerId)->cursorX > 0) {
        std::swap(owners[0], owners[1]);
    }
    const world::EntityHandle kBuilder = owners[0];
    const world::EntityHandle kOther = owners[1];
    std::vector<world::EntityHandle> stalls = holding(world, kStallId);
    RAWFRAME_EXPECT(stalls.size() == 1);
    if (stalls.size() != 1) {
        composition.stop();
        return;
    }
    const world::EntityHandle kStall = stalls[0];
    const auto kLevel = [&] {
        return valueOf<Stall>(world, kStall, kStallId)->level;
    };
    const auto kOwner = [&](world::EntityHandle entity) {
        return *valueOf<Owner>(world, entity, kOwnerId);
    };
    RAWFRAME_EXPECT(valueOf<Stall>(world, kStall, kStallId)->owner == kBuilder && kLevel() == 1);
    RAWFRAME_EXPECT(kOwner(kBuilder).coins == 50 && kOwner(kBuilder).stalls == 1 && kOwner(kBuilder).levels == 1);

    // Held, the press raises nothing more; let go and pressed again, it
    // raises the stall a level for the level's cost.
    const auto kPress = [&](world::EntityHandle owner, float build) {
        valueOf<Hand>(world, owner, kHandId)->build = build;
        kRun(2);
    };
    kRun(2);
    RAWFRAME_EXPECT(kLevel() == 1);
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 2 && kOwner(kBuilder).coins == 0 && kOwner(kBuilder).stalls == 1 &&
                    kOwner(kBuilder).levels == 2);

    // Another owner pressing on it raises nothing and spends nothing.
    Hand& other = *valueOf<Hand>(world, kOther, kHandId);
    other.pointX = -22;
    other.pointZ = -22;
    kPress(kOther, 1);
    RAWFRAME_EXPECT(kLevel() == 2 && kOwner(kOther).coins == 100 && kOwner(kOther).stalls == 0 &&
                    holding(world, kStallId).size() == 1);

    // Short of the next level's cost, nothing; with it, the highest level,
    // and past that nothing, whatever the owner holds.
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 2);
    valueOf<Owner>(world, kBuilder, kOwnerId)->coins = 1000;
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 3 && kOwner(kBuilder).coins == 900 && kOwner(kBuilder).levels == 3);
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 3 && kOwner(kBuilder).coins == 900);

    // Each second, an owner earns for every level of its stalls.
    kPress(kBuilder, 0);
    kRun(60);
    RAWFRAME_EXPECT(kOwner(kBuilder).coins == 915);
    composition.stop();
    simulation = nullptr;
}
