// Hostile calls into the engine's doors (the rules' fuzzing duty for
// Kest-facing doors): a program passes each physics query NaN, the
// infinities, the largest and smallest numbers, negative sizes, classes and
// indices past any there are, and moments far from any kept, argument by
// argument and all at once; and makes, marks, and destroys entities twice
// over. Every call answers, the World keeps stepping, and the sanitizers
// of the full check find nothing.

#include "../src/predictor.h"
#include "game_harness.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
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

/// Plays the test game `name`, held as a web client holds its files, for
/// `ticks` ticks at 60 Hz and hands its World to `check` before it stops.
template <typename Check> void play(std::string_view name, std::uint64_t ticks, const Check& check) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{.registrars = kWithPhysics,
                                        .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    const std::string kText = "kest.game = game/" + std::string{name} + "\n" + "world.tick_rate = 60\n" +
                              "world.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration,
                                                                   .files = &heldGames()}};
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

RAWFRAME_TEST(LookupsOfWhatIsNotThereAnswerWithoutHarm) {
    play("peek.game", 3, [](world::World& world) {
        // Of thirteen asked, the three marks alone are found, at their
        // places; the count is right; nothing failed.
        const auto kProbe =
            world.registry().find(schema::ComponentTypeId::fromText("3d6c1e2a-8f47-4b09-a5d1-7c2e9f0b4a63"));
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kProbe, world::Access::Read}};
        auto probes = world::ColumnQuery::resolve(kTerms, world.registry());
        std::array<std::uint32_t, 3> seen{};
        probes->forEachChunk(world, [&seen](const world::ColumnChunk& chunk) {
            std::memcpy(seen.data(), chunk.columns[0], sizeof seen);
        });
        RAWFRAME_EXPECT(seen[0] == 14 && seen[1] == 3 && seen[2] == 21);
    });
}

RAWFRAME_TEST(LifespansEndAndTimersFireAsTheEngineCountsThem) {
    play("fuse.game", 7, [](world::World& world) {
        // Lifespans of two and five ticks are both spent, their entities
        // gone; the lamps are left.
        const auto kLife = schema::ComponentTypeId::fromText("ca6ab7bf-286c-4b1f-b99d-a206b78927f8");
        const auto kBlink = schema::ComponentTypeId::fromText("a3977daa-795c-4b87-90ac-ffee606220e7");
        const auto kCount = schema::ComponentTypeId::fromText("50918e2c-1773-45a7-8f31-c144f1b15ef4");
        RAWFRAME_EXPECT(holding(world, kLife).empty() && world.entityCount() == 2);
        struct Timer {
            std::uint32_t left;
            std::uint32_t period;
            bool fired;
        };
        // The lamp of period three fired on ticks three and six and has two
        // to go; the lamp of four fired once and stopped. Neither fired on
        // the seventh, and the simulation phase saw every firing.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (const world::EntityHandle kLamp : holding(world, kBlink)) {
            const Timer* const kTimer = valueOf<Timer>(world, kLamp, kBlink);
            const auto* const kFired = valueOf<std::uint32_t>(world, kLamp, kCount);
            RAWFRAME_EXPECT(kTimer != nullptr && kFired != nullptr && !kTimer->fired);
            if (kTimer != nullptr && kFired != nullptr) {
                seen.emplace_back(kTimer->left, *kFired);
            }
        }
        std::ranges::sort(seen);
        RAWFRAME_EXPECT(seen == (std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 1}, {2, 2}}));
    });
}

RAWFRAME_TEST(APredictorCountsTimersAsTheServerDoes) {
    // The fuse game's tally predicted, over a player that holds a lamp: the
    // predictor counts its timer, so the tally sees each firing the server's
    // would.
    std::string game = readText(std::string{RAWFRAME_WORLD_KEST_GAMES} + "fuse.game");
    const std::string kTally = "system fuse.tally simulation tally read fuse.blink write fuse.count";
    game.replace(game.find(kTally), kTally.size(), kTally + " predicted");
    game += "component 1f7c0d52-8e3b-4a96-b4d1-6c2e9a7f3b80 fuse.press Count\n"
            "player fuse.blink fuse.count fuse.press\n"
            "input fuse.press\n";
    const std::string kProgram = readText(std::string{RAWFRAME_WORLD_KEST_GAMES} + "fuse.kest");
    const auto kFiles = world_kest::GameFiles::fromHeld("fuse.game", {{"fuse.game", game}, {"fuse.kest", kProgram}});
    RAWFRAME_EXPECT(kFiles.has_value());
    if (!kFiles.has_value()) {
        return;
    }
    const auto kCompiled = kFiles->compile("fuse.kest", {});
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        return;
    }
    std::vector<schema::ComponentDescriptor> descriptors;
    for (const world_kest::GameComponent& component : kFiles->description().components) {
        const auto kLayout = world_kest::componentLayout(kFiles->description(), **kCompiled, component);
        RAWFRAME_EXPECT(kLayout.has_value());
        if (!kLayout.has_value()) {
            return;
        }
        descriptors.push_back({.id = component.id,
                               .name = component.name,
                               .size = kLayout->size,
                               .alignment = kLayout->alignment,
                               .plainData = true});
    }
    const auto kBlink = schema::ComponentTypeId::fromText("a3977daa-795c-4b87-90ac-ffee606220e7");
    const auto kCount = schema::ComponentTypeId::fromText("50918e2c-1773-45a7-8f31-c144f1b15ef4");
    const auto kFiredAfter = [&](std::optional<world_kest::GameTimerSettings> timers) -> std::uint32_t {
        auto predictor = world_kest::makePredictor(
            world_kest::PredictorSettings{.program = *kCompiled,
                                          .game = &kFiles->description(),
                                          .descriptors = descriptors,
                                          .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000},
                                          .timers = std::move(timers)});
        RAWFRAME_EXPECT(predictor.has_value());
        if (!predictor.has_value()) {
            return 0;
        }
        // A lamp of period three, as the server confirmed it.
        const std::array<std::uint32_t, 3> kLamp = {3, 3, 0};
        RAWFRAME_EXPECT((*predictor)->set(kBlink, std::as_bytes(std::span{kLamp})).has_value());
        const std::array<std::byte, 4> kNoPress{};
        for (std::uint64_t tick = 1; tick <= 6; ++tick) {
            RAWFRAME_EXPECT((*predictor)->step(kNoPress, world::TickIndex{tick}).has_value());
        }
        std::uint32_t fired = 0;
        const std::span<const std::byte> kHeld = (*predictor)->get(kCount);
        RAWFRAME_EXPECT(kHeld.size() == sizeof fired);
        if (kHeld.size() == sizeof fired) {
            std::memcpy(&fired, kHeld.data(), sizeof fired);
        }
        return fired;
    };
    // Six ticks fire it on the third and the sixth; without the count it
    // would never fire.
    RAWFRAME_EXPECT(kFiredAfter(world_kest::GameTimerSettings{.lifespans = {}, .timers = {kBlink}}) == 2);
    RAWFRAME_EXPECT(kFiredAfter(std::nullopt) == 0);

    // A player whose lifespan runs out while predicting is counted to
    // nought and kept: the server destroys it, and the predictor steps on.
    std::string spent = game;
    spent.replace(spent.find("player fuse.blink fuse.count fuse.press"),
                  std::string_view{"player fuse.blink fuse.count fuse.press"}.size(),
                  "player fuse.blink fuse.count fuse.press fuse.life");
    const auto kSpentFiles =
        world_kest::GameFiles::fromHeld("fuse.game", {{"fuse.game", spent}, {"fuse.kest", kProgram}});
    RAWFRAME_EXPECT(kSpentFiles.has_value());
    if (!kSpentFiles.has_value()) {
        return;
    }
    const auto kLife = schema::ComponentTypeId::fromText("ca6ab7bf-286c-4b1f-b99d-a206b78927f8");
    auto predictor = world_kest::makePredictor(world_kest::PredictorSettings{
        .program = *kCompiled,
        .game = &kSpentFiles->description(),
        .descriptors = descriptors,
        .limits = {.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000},
        .timers = world_kest::GameTimerSettings{.lifespans = {kLife}, .timers = {kBlink}}});
    RAWFRAME_EXPECT(predictor.has_value());
    if (!predictor.has_value()) {
        return;
    }
    const std::uint32_t kTwoTicks = 2;
    RAWFRAME_EXPECT((*predictor)->set(kLife, std::as_bytes(std::span{&kTwoTicks, 1})).has_value());
    const std::array<std::byte, 4> kNoPress{};
    for (std::uint64_t tick = 1; tick <= 4; ++tick) {
        RAWFRAME_EXPECT((*predictor)->step(kNoPress, world::TickIndex{tick}).has_value());
    }
    std::uint32_t left = 7;
    const std::span<const std::byte> kHeld = (*predictor)->get(kLife);
    RAWFRAME_EXPECT(kHeld.size() == sizeof left);
    if (kHeld.size() == sizeof left) {
        std::memcpy(&left, kHeld.data(), sizeof left);
    }
    RAWFRAME_EXPECT(left == 0);
    RAWFRAME_EXPECT((*predictor)->set(kLife, std::as_bytes(std::span{&kTwoTicks, 1})).has_value());
}

RAWFRAME_TEST(ASystemLearnsWhatCameAndWent) {
    play("relics.game", 6, [](world::World& world) {
        struct Keeper {
            std::uint32_t added;
            std::uint32_t removed;
            std::uint32_t removedSum;
            std::uint32_t destroyed;
        };
        const auto kKeeper = schema::ComponentTypeId::fromText("f3b74d26-91e5-4c0a-b7d8-2e6a1c9f4053");
        const auto kHeld = holding(world, kKeeper);
        RAWFRAME_EXPECT(kHeld.size() == 1);
        if (kHeld.size() != 1) {
            return;
        }
        const Keeper* const kSeen = valueOf<Keeper>(world, kHeld[0], kKeeper);
        // Changes reach the keeper the tick after their barrier. Its first
        // run is given the two gems spawned before it; then each tick's
        // gem one tick on: seven added by the sixth run. The gems of two and
        // four lost their gems and the gem of three was destroyed, which
        // removed its gem too: three removed, holding 2 + 3 + 4, and one
        // destroyed. The gem of six was removed on the last tick, after
        // the keeper ran.
        RAWFRAME_EXPECT(kSeen != nullptr && kSeen->added == 7 && kSeen->removed == 3 && kSeen->removedSum == 9 &&
                        kSeen->destroyed == 1);
        // What every reader has read is let go: nothing is held past the
        // keeper's last run but the last tick's barrier.
        RAWFRAME_EXPECT(world.lifecycle().readers().size() == 1 &&
                        world.lifecycle().unread("relics.watch").size() == world.lifecycle().changes().size());
    });
}
