// The schedule and simulation time: order, refusals, the commit barrier,
// failure isolation, determinism, and tick pacing.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"
#include "rawframe/world/query.h"
#include "rawframe/world/schedule.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace rawframe::world;
using namespace rawframe::world::testing;
using rawframe::execution::MonotonicDuration;
using rawframe::execution::MonotonicInstant;
using rawframe::result::Status;

namespace {

/// Records its name in a shared log each time it runs.
class Logging final : public System {
public:
    Logging(std::string name, std::vector<std::string>& log) : name_(std::move(name)), log_(&log) {
    }
    Status run(SystemContext&) noexcept override {
        log_->push_back(name_);
        return {};
    }

private:
    std::string name_;
    std::vector<std::string>* log_;
};

bool refusedWith(const auto& outcome, WorldError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(SystemsRunByPhaseThenEdgesThenIdentity) {
    std::vector<std::string> log;
    Logging zeta{"zeta", log}, alpha{"alpha", log}, beta{"beta", log}, early{"early", log}, sealing{"sealing", log};
    constexpr std::string_view kAfterZeta[] = {"zeta"};
    const SystemDeclaration kDeclarations[] = {
        {.identity = "zeta", .phase = Phase::Simulation, .system = &zeta},
        {.identity = "alpha", .phase = Phase::Simulation, .after = kAfterZeta, .system = &alpha},
        {.identity = "sealing", .phase = Phase::Commit, .system = &sealing},
        {.identity = "beta", .phase = Phase::Simulation, .system = &beta},
        {.identity = "early", .phase = Phase::BeginTick, .system = &early},
    };
    const auto kRegistry = makeRegistry();
    auto schedule = Schedule::compile(kDeclarations, *kRegistry);
    RAWFRAME_EXPECT(schedule.has_value());
    if (!schedule.has_value()) {
        return;
    }
    const std::vector<std::string> kExpected = {"early", "beta", "zeta", "alpha", "sealing"};
    RAWFRAME_EXPECT(schedule->order() == kExpected);
    World world{kRegistry};
    TickIndex tick;
    RAWFRAME_EXPECT(schedule->runTick(world, tick, TickRate{}).has_value());
    RAWFRAME_EXPECT(log == kExpected);
    RAWFRAME_EXPECT(tick.value == 1);
}

RAWFRAME_TEST(BadSchedulesAreRefused) {
    std::vector<std::string> log;
    Logging one{"one", log}, two{"two", log};
    const auto kRegistry = makeRegistry();
    constexpr std::string_view kGhost[] = {"ghost"};
    constexpr std::string_view kOne[] = {"one"};
    constexpr std::string_view kTwo[] = {"two"};
    const rawframe::schema::ComponentRuntimeId kOutside[] = {{99}};

    const SystemDeclaration kDuplicate[] = {{.identity = "one", .system = &one}, {.identity = "one", .system = &two}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kDuplicate, *kRegistry), WorldError::DuplicateSystem));

    const SystemDeclaration kUnknown[] = {{.identity = "one", .after = kGhost, .system = &one}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kUnknown, *kRegistry), WorldError::UnknownSystem));

    const SystemDeclaration kCrossPhase[] = {
        {.identity = "one", .phase = Phase::Simulation, .system = &one},
        {.identity = "two", .phase = Phase::Commit, .after = kOne, .system = &two}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kCrossPhase, *kRegistry), WorldError::CrossPhaseOrdering));

    const SystemDeclaration kCycle[] = {{.identity = "one", .after = kTwo, .system = &one},
                                        {.identity = "two", .after = kOne, .system = &two}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kCycle, *kRegistry), WorldError::SystemCycle));

    const SystemDeclaration kAccess[] = {{.identity = "one", .writes = kOutside, .system = &one}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kAccess, *kRegistry), WorldError::UndeclaredAccess));

    const SystemDeclaration kMissing[] = {{.identity = "one"}};
    RAWFRAME_EXPECT(refusedWith(Schedule::compile(kMissing, *kRegistry), WorldError::UnknownSystem));
}

namespace {

/// Spawns one positioned entity per tick through its command buffer, and
/// checks that direct structural change is refused while it runs.
class Spawner final : public System {
public:
    explicit Spawner(Keys keys) : keys_(keys) {
    }
    Status run(SystemContext& context) noexcept override {
        directRefused = !context.world.create().has_value();
        RAWFRAME_TRY_ASSIGN(const PendingEntity kPending, context.commands.create());
        return context.commands.insert(kPending, keys_.position, Position{1, 0});
    }
    bool directRefused = false;

private:
    Keys keys_;
};

/// Counts positioned entities when it runs.
class Counter final : public System {
public:
    explicit Counter(const rawframe::schema::SchemaRegistry& registry)
        : query_(*Query<Read<Position>>::resolve(registry)) {
    }
    Status run(SystemContext& context) noexcept override {
        counts.push_back(query_.count(context.world));
        return {};
    }
    std::vector<std::size_t> counts;

private:
    Query<Read<Position>> query_;
};

class Failing final : public System {
public:
    explicit Failing(Keys keys) : keys_(keys) {
    }
    Status run(SystemContext& context) noexcept override {
        RAWFRAME_TRY(context.commands.create());
        return rawframe::result::fail(rawframe::result::ErrorClass::Internal,
                                      kWorldDomain,
                                      rawframe::result::ErrorCode{999},
                                      "this system always fails");
    }

private:
    Keys keys_;
};

} // namespace

RAWFRAME_TEST(CommandsBecomeVisibleAtTheCommitBarrier) {
    const auto kRegistry = makeRegistry();
    const Keys kKeys = keysOf(*kRegistry);
    Spawner spawner{kKeys};
    Counter beforeCommit{*kRegistry};
    Counter atCommit{*kRegistry};
    Failing failing{kKeys};
    const SystemDeclaration kDeclarations[] = {
        {.identity = "spawner", .phase = Phase::Simulation, .system = &spawner},
        {.identity = "failing", .phase = Phase::Simulation, .system = &failing},
        {.identity = "before_commit", .phase = Phase::PostSimulation, .system = &beforeCommit},
        {.identity = "at_commit", .phase = Phase::Commit, .system = &atCommit},
    };
    auto schedule = Schedule::compile(kDeclarations, *kRegistry);
    World world{kRegistry};
    TickIndex tick;
    for (int round = 0; round < 3; ++round) {
        const auto kReport = schedule->runTick(world, tick, TickRate{});
        RAWFRAME_EXPECT(kReport.has_value());
        if (kReport.has_value()) {
            RAWFRAME_EXPECT(kReport->failures.size() == 1 && kReport->failures[0].system == "failing");
            RAWFRAME_EXPECT(kReport->commandsApplied == 2);
        }
    }
    RAWFRAME_EXPECT(spawner.directRefused);
    RAWFRAME_EXPECT((beforeCommit.counts == std::vector<std::size_t>{0, 1, 2}));
    RAWFRAME_EXPECT((atCommit.counts == std::vector<std::size_t>{1, 2, 3}));
    // The failing system's entity was never created.
    RAWFRAME_EXPECT(world.entityCount() == 3);
    RAWFRAME_EXPECT(!world.structureLocked());
}

namespace {

class Integrate final : public System {
public:
    explicit Integrate(const rawframe::schema::SchemaRegistry& registry)
        : query_(*Query<Write<Position>, Read<Velocity>>::resolve(registry)) {
    }
    Status run(SystemContext& context) noexcept override {
        query_.forEach(context.world, [](EntityHandle, Position& position, const Velocity& velocity) {
            position.x += velocity.dx;
            position.y += velocity.dy;
        });
        return {};
    }

private:
    Query<Write<Position>, Read<Velocity>> query_;
};

/// Runs 200 ticks of a fixed scenario and folds the final state into a hash.
std::uint64_t simulate() {
    const auto kRegistry = makeRegistry();
    const Keys kKeys = keysOf(*kRegistry);
    World world{kRegistry};
    for (std::int32_t index = 0; index < 100; ++index) {
        const EntityHandle kEntity = *world.create();
        static_cast<void>(world.insert(kEntity, kKeys.position, Position{index, -index}));
        static_cast<void>(world.insert(kEntity, kKeys.velocity, Velocity{index % 7 - 3, index % 5 - 2}));
    }
    Integrate integrate{*kRegistry};
    const SystemDeclaration kDeclarations[] = {{.identity = "integrate", .system = &integrate}};
    auto schedule = Schedule::compile(kDeclarations, *kRegistry);
    TickIndex tick;
    for (int round = 0; round < 200; ++round) {
        static_cast<void>(schedule->runTick(world, tick, TickRate{}));
    }
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    auto all = Query<Read<Position>>::resolve(*kRegistry);
    all->forEach(world, [&hash](EntityHandle entity, const Position& position) {
        for (const std::uint64_t kPart : {std::uint64_t{entity.slot},
                                          static_cast<std::uint64_t>(position.x),
                                          static_cast<std::uint64_t>(position.y)}) {
            hash = (hash ^ kPart) * 0x100000001B3ULL;
        }
    });
    return hash;
}

} // namespace

RAWFRAME_TEST(TheSameScenarioProducesTheSameWorld) {
    RAWFRAME_EXPECT(simulate() == simulate());
}

RAWFRAME_TEST(DeclaredAccessDecidesConflicts) {
    std::vector<std::string> log;
    Logging reader{"reader", log}, writer{"writer", log}, other{"other", log}, alone{"alone", log};
    const auto kRegistry = makeRegistry();
    const Keys kKeys = keysOf(*kRegistry);
    const rawframe::schema::ComponentRuntimeId kPosition[] = {kKeys.position.id};
    const rawframe::schema::ComponentRuntimeId kVelocity[] = {kKeys.velocity.id};
    const SystemDeclaration kDeclarations[] = {
        {.identity = "reader", .reads = kPosition, .system = &reader},
        {.identity = "writer", .writes = kPosition, .system = &writer},
        {.identity = "other", .writes = kVelocity, .system = &other},
        {.identity = "alone", .exclusive = true, .system = &alone},
    };
    auto schedule = Schedule::compile(kDeclarations, *kRegistry);
    RAWFRAME_EXPECT(schedule->conflicts("reader", "writer"));
    RAWFRAME_EXPECT(!schedule->conflicts("reader", "other"));
    RAWFRAME_EXPECT(!schedule->conflicts("writer", "other"));
    RAWFRAME_EXPECT(schedule->conflicts("alone", "other"));
}

RAWFRAME_TEST(PacingBoundsCatchUpAndKeepsTheDebt) {
    RAWFRAME_EXPECT(!TickRate::of(0).has_value() && !TickRate::of(60, 0).has_value());
    const TickRate kRate = *TickRate::of(60);
    TickPacer pacer{kRate, 8, MonotonicInstant{}};
    const MonotonicInstant kOneSecond = MonotonicInstant{} + MonotonicDuration::fromSeconds(1);
    auto due = pacer.due(kOneSecond);
    RAWFRAME_EXPECT(due.run == 8 && due.debt == 52);
    pacer.ran(due.run);
    due = pacer.due(kOneSecond);
    RAWFRAME_EXPECT(due.run == 8 && due.debt == 44);
    pacer.ran(52);
    RAWFRAME_EXPECT(pacer.due(kOneSecond).run == 0);
    // One tick is owed only once its whole duration has passed.
    RAWFRAME_EXPECT(pacer.due(kOneSecond + MonotonicDuration{16'666'666}).run == 0);
    RAWFRAME_EXPECT(pacer.due(kOneSecond + MonotonicDuration{16'666'667}).run == 1);

    // NTSC-style rates stay exact.
    const TickRate kNtsc = *TickRate::of(30000, 1001);
    RAWFRAME_EXPECT(kNtsc.ticksIn(MonotonicDuration::fromMilliseconds(1001)) == 30);
    RAWFRAME_EXPECT(kNtsc.ticksIn(MonotonicDuration::fromSeconds(1001LL * 3600)) == 30000ULL * 3600);
}
