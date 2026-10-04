// Stalls played: an owner builds a stall, raises it a level with another
// press, and up to the highest level and no further; another owner pressing
// on that stall raises nothing (D391). Customers come to the stall, are
// served, and what they pay reaches its owner, no customer staying past its
// patience (D392). An owner reaching the goal wins the round, and the lot
// starts afresh (D394). The test moves the owners' hands between ticks, as
// their clients' input would.

#include "game_harness.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
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
    std::int64_t collected = 0;
};

struct Stall {
    world::EntityHandle owner;
    std::int32_t plotX = 0;
    std::int32_t plotZ = 0;
    std::uint32_t level = 0;
};

struct Till {
    std::int64_t takings = 0;
};

constexpr auto kTillId = schema::ComponentTypeId::fromText("160c062b-09b3-46a0-8435-9575f99339c1");
constexpr auto kRoundId = schema::ComponentTypeId::fromText("7c496087-7144-4230-85e9-fd119433276c");
constexpr auto kCustomerId = schema::ComponentTypeId::fromText("bd13f2d3-dc87-46f2-b480-ff2eaf5fcdf1");

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

/// Stalls' levels game, composed and started, ticked at 60 Hz on a
/// manual clock.
class Played {
public:
    Played() {
        std::vector<composition::Problem> problems;
        auto plan = composition::compose(
            composition::CompositionRequest{.registrars = kWithPhysics,
                                            .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
            problems);
        const std::string kText = "kest.game = " + std::string{RAWFRAME_SAMPLE_GAMES} + "tycoon/levels.game\n" +
                                  "world.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n";
        auto configuration = composition::Configuration::parse(kText);
        if (!plan.has_value() || !configuration.has_value()) {
            return;
        }
        plan_.emplace(std::move(*plan));
        configuration_.emplace(std::move(*configuration));
        composition_.emplace(*plan_,
                             composition::HostServices{.clock = &clock_,
                                                       .scope = &root_,
                                                       .cpu = &test::cpuExecutor(),
                                                       .blockingIo = &test::blockingIoExecutor(),
                                                       .configuration = &*configuration_});
        started_ = composition_->start().has_value();
    }

    Played(const Played&) = delete;
    Played& operator=(const Played&) = delete;

    ~Played() {
        if (started_) {
            composition_->stop();
        }
        simulation = nullptr;
    }

    [[nodiscard]] bool started() const noexcept {
        return started_;
    }

    void run(std::uint64_t ticks) {
        for (std::uint64_t tick = 0; tick < ticks; ++tick) {
            clock_.advance(execution::MonotonicDuration{16'666'667});
            composition_->runHostPhase(composition::HostPhase::RunWorlds,
                                       composition::HostFrame{.iteration = iteration_++, .now = clock_.now()});
        }
    }

private:
    std::optional<composition::Plan> plan_;
    std::optional<composition::Configuration> configuration_;
    execution::ManualClock clock_;
    execution::CancellationScope root_{clock_};
    std::optional<composition::Composition> composition_;
    std::uint64_t iteration_ = 0;
    bool started_ = false;
};

} // namespace

RAWFRAME_TEST(AnOwnerRaisesItsOwnStallsAndNoOneElses) {
    Played played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    const auto kRun = [&played](std::uint64_t ticks) {
        played.run(ticks);
    };
    world::World& world = *simulation->world();

    // The first owner builds on its first tick and has spent the cost.
    kRun(3);
    std::vector<world::EntityHandle> owners = holding(world, kOwnerId);
    RAWFRAME_EXPECT(owners.size() == 2);
    if (owners.size() != 2) {
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
        return;
    }
    const world::EntityHandle kStall = stalls[0];
    // Nought, or a zeroed owner, for one gone.
    const auto kLevel = [&] {
        const Stall* const kFound = valueOf<Stall>(world, kStall, kStallId);
        return kFound != nullptr ? kFound->level : 0;
    };
    const auto kOwner = [&](world::EntityHandle entity) {
        const Owner* const kFound = valueOf<Owner>(world, entity, kOwnerId);
        return kFound != nullptr ? *kFound : Owner{};
    };
    RAWFRAME_EXPECT(kLevel() == 1 && valueOf<Stall>(world, kStall, kStallId)->owner == kBuilder);
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
    valueOf<Owner>(world, kBuilder, kOwnerId)->coins = 500;
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 3 && kOwner(kBuilder).coins == 400 && kOwner(kBuilder).levels == 3);
    kPress(kBuilder, 0);
    kPress(kBuilder, 1);
    RAWFRAME_EXPECT(kLevel() == 3 && kOwner(kBuilder).coins == 400);

    // Each second, an owner earns for every level of its stalls.
    kPress(kBuilder, 0);
    kRun(60);
    RAWFRAME_EXPECT(kOwner(kBuilder).coins == 406);
}

RAWFRAME_TEST(CustomersBuyAtAStallAndItsOwnerTakesWhatTheyPay) {
    Played played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    // Nobody comes while no stall stands; then the first owner's stall
    // stands, and for half a minute customers walk to it and buy.
    world::World& world = *simulation->world();
    played.run(1);
    std::vector<world::EntityHandle> stalls = holding(world, kStallId);
    RAWFRAME_EXPECT(stalls.size() == 1 && holding(world, kCustomerId).empty());
    if (stalls.size() != 1) {
        return;
    }
    const world::EntityHandle kStall = stalls[0];
    const world::EntityHandle kBuilder = valueOf<Stall>(world, kStall, kStallId)->owner;
    std::int64_t served = 0;
    std::size_t most = 0;
    for (int second = 0; second < 30; ++second) {
        played.run(60);
        most = std::max(most, holding(world, kCustomerId).size());
    }
    served = valueOf<Till>(world, kStall, kTillId)->takings;
    // One came every forty ticks, 45 in all, each walking some twenty
    // seconds across the lot; each served paid four at a stall of the first
    // level and is gone, and the rest are still walking.
    const std::size_t kWalking = holding(world, kCustomerId).size();
    RAWFRAME_EXPECT(served > 0 && served % 4 == 0 && kWalking + static_cast<std::size_t>(served / 4) == 45 &&
                    most >= kWalking);
    const Owner kOwner = *valueOf<Owner>(world, kBuilder, kOwnerId);
    // The owner took the till as of its last second: everything but what
    // the last moments brought.
    RAWFRAME_EXPECT(kOwner.collected > 0 && kOwner.collected <= served && served - kOwner.collected <= 16);
    // Its coins: what it started with, less the stall, its income, and
    // what it took.
    RAWFRAME_EXPECT(kOwner.coins == 100 - 50 + (2 * 30) + kOwner.collected);
}

RAWFRAME_TEST(TheFirstOwnerToTheGoalWinsTheRoundAndTheLotStartsAfresh) {
    Played played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    played.run(3);
    const std::vector<world::EntityHandle> kRounds = holding(world, kRoundId);
    const std::vector<world::EntityHandle> kStalls = holding(world, kStallId);
    RAWFRAME_EXPECT(kRounds.size() == 1 && kStalls.size() == 1);
    if (kRounds.size() != 1 || kStalls.size() != 1) {
        return;
    }
    const auto kNumber = [&] {
        return *valueOf<std::uint32_t>(world, kRounds[0], kRoundId);
    };
    const world::EntityHandle kBuilder = valueOf<Stall>(world, kStalls[0], kStallId)->owner;
    // Short of the goal, the round goes on; the second's income takes the
    // builder past it, and the round ends that tick.
    valueOf<Owner>(world, kBuilder, kOwnerId)->coins = 999;
    played.run(50);
    RAWFRAME_EXPECT(kNumber() == 0 && holding(world, kStallId).size() == 1);
    played.run(10);
    RAWFRAME_EXPECT(kNumber() == 1);
    // Every owner starts again, and no stall or customer stands.
    RAWFRAME_EXPECT(holding(world, kStallId).empty() && holding(world, kCustomerId).empty());
    for (const world::EntityHandle kOwner : holding(world, kOwnerId)) {
        const Owner& owner = *valueOf<Owner>(world, kOwner, kOwnerId);
        RAWFRAME_EXPECT(owner.coins == 100 && owner.stalls == 0 && owner.levels == 0 && owner.collected == 0);
    }
    // The next round is played on: once let go and pressed again, the
    // builder builds anew.
    Hand& hand = *valueOf<Hand>(world, kBuilder, kHandId);
    hand.build = 0;
    played.run(2);
    valueOf<Hand>(world, kBuilder, kHandId)->build = 1;
    played.run(2);
    RAWFRAME_EXPECT(holding(world, kStallId).size() == 1 && valueOf<Owner>(world, kBuilder, kOwnerId)->coins == 50 &&
                    kNumber() == 1);
}
