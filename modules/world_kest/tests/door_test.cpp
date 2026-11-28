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
#include "rawframe/world/persistent.h"
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

RAWFRAME_TEST(AQueueCarriesEachMessageToEveryReaderOnce) {
    play("relay.game", 5, [](world::World& world) {
        struct Tally {
            std::uint32_t seen;
            std::uint64_t last;
        };
        const auto kLate = schema::ComponentTypeId::fromText("37806ddb-7230-480a-90c9-7c4dd2b2905e");
        const auto kEarly = schema::ComponentTypeId::fromText("0d45e089-17c5-43e9-acef-b1950e8d672a");
        const auto kLateHeld = holding(world, kLate);
        const auto kEarlyHeld = holding(world, kEarly);
        RAWFRAME_EXPECT(kLateHeld.size() == 1 && kEarlyHeld.size() == 1);
        if (kLateHeld.size() != 1 || kEarlyHeld.size() != 1) {
            return;
        }
        const Tally* const kAfter = valueOf<Tally>(world, kLateHeld[0], kLate);
        const Tally* const kBefore = valueOf<Tally>(world, kEarlyHeld[0], kEarly);
        // Five pings: the reader after the writer read each the tick it was
        // written, the one before it each but the last the tick after, and
        // neither read one twice.
        RAWFRAME_EXPECT(kAfter != nullptr && kBefore != nullptr);
        if (kAfter != nullptr && kBefore != nullptr) {
            RAWFRAME_EXPECT(kAfter->seen == 5 && kBefore->seen == 4 && kAfter->last == kBefore->last + 1);
        }
        // The World's queue holds what is not yet over: the last two ticks'.
        const world::MessageQueue* const kPings = world.queues().find("pings");
        RAWFRAME_EXPECT(kPings != nullptr && kPings->messages().size() == 2 && kPings->nextSequence() == 5);
    });
}

RAWFRAME_TEST(AClientBindsAProgramWithQueues) {
    // The relay's program, given a present system and a predicted one: a
    // client's presentation and its predictor bind the queue doors too.
    const std::string kGame = readText(std::string{RAWFRAME_WORLD_KEST_GAMES} + "relay.game") +
                              "player relay.sender\n"
                              "input relay.sender\n"
                              "system relay.idle simulation idle write relay.sender predicted\n"
                              "component 7b1c9e24-5a3d-4f86-b0e2-9d4c6a8f1e35 relay.shown Tally\n"
                              "presentation relay.shown on player\n"
                              "present relay.show early write relay.shown\n";
    const std::string kProgram = readText(std::string{RAWFRAME_WORLD_KEST_GAMES} + "relay.kest") +
                                 "\nfn idle(count: i32, senders: [Sender]) {\n}\n";
    const auto kFiles =
        world_kest::GameFiles::fromHeld("relay.game", {{"relay.game", kGame}, {"relay.kest", kProgram}});
    RAWFRAME_EXPECT(kFiles.has_value());
    if (!kFiles.has_value()) {
        return;
    }
    const auto kCompiled = kFiles->compile("relay.kest", {});
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
    // rawframe.world's persistent identity, which a World registers itself
    // and the description already names.
    RAWFRAME_EXPECT(
        std::ranges::contains(descriptors, world::Persistent::kComponentTypeId, &schema::ComponentDescriptor::id));
    const kest::MachineLimits kLimits{.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000};
    auto presentation = world_kest::ClientPresentation::create(world_kest::PresentationSettings{
        .program = *kCompiled, .game = &kFiles->description(), .descriptors = descriptors, .limits = kLimits});
    RAWFRAME_EXPECT(presentation.has_value() && *presentation != nullptr);
    if (!presentation.has_value() || *presentation == nullptr) {
        return;
    }
    schema::RegistryBuilder builder;
    for (const schema::ComponentDescriptor& descriptor : descriptors) {
        builder.add(descriptor);
    }
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const world::EntityHandle kPlayer = *mirror.create();
    // Bound; the present system that reads the queue is refused for it, and
    // the client goes on.
    RAWFRAME_EXPECT((*presentation)->present(mirror, kPlayer, {}, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT((*presentation)->statistics().systemsFailed == 1);
    auto predictor = world_kest::makePredictor(world_kest::PredictorSettings{
        .program = *kCompiled, .game = &kFiles->description(), .descriptors = descriptors, .limits = kLimits});
    RAWFRAME_EXPECT(predictor.has_value());
}
