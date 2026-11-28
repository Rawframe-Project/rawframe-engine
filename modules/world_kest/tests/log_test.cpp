// A game's log records: a system's `rawframe.log` calls and `std.io`'s
// `print` each a record naming the system and the tick, bounded by run,
// repeats counted, a level or a system left out as configuration says; a
// client's present systems log through its own emitter, and a predicting
// client binds the doors and writes nothing.

#include "../src/predictor.h"
#include "../src/presentation.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/diagnostics/router.h"
#include "rawframe/diagnostics/sink.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/test/test.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_kest/log_doors.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

/// What a record said, kept past the call that delivered it.
struct Kept {
    std::string code;
    diagnostics::Severity severity = diagnostics::Severity::Info;
    std::string system;
    std::string text;
    std::int64_t tick = -1;
    std::int64_t repeated = -1;
    std::int64_t overBound = -1;
    bool aboutEntity = false;
};

class Keeper final : public diagnostics::Sink {
public:
    void accept(const diagnostics::Record& record) noexcept override {
        Kept kept{.code = std::string{record.identity.code}, .severity = record.severity};
        for (const diagnostics::Field& field : record.fields) {
            const std::string_view kKey = field.key.text;
            if (kKey == "system") {
                kept.system = field.value.text;
            } else if (kKey == "text") {
                kept.text = field.value.text;
            } else if (kKey == "tick") {
                kept.tick = field.value.integer;
            } else if (kKey == "repeated") {
                kept.repeated = field.value.integer;
            } else if (kKey == "overBound") {
                kept.overBound = field.value.integer;
            } else if (kKey == "entitySlot") {
                kept.aboutEntity = true;
            }
        }
        records.push_back(std::move(kept));
    }

    /// The game's own records, in order.
    std::vector<Kept> written() const {
        std::vector<Kept> found;
        for (const Kept& each : records) {
            if (each.code == "game_log") {
                found.push_back(each);
            }
        }
        return found;
    }

    std::vector<Kept> records;
};

/// A router delivering everything, down to debug, to one keeper.
struct Logged {
    Keeper keeper;
    std::array<diagnostics::Sink*, 1> sinks{&keeper};
    diagnostics::Router router{diagnostics::RouterSettings{.minimumSeverity = diagnostics::Severity::Debug}, sinks};
};

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("6a0d3e1f-52b4-4c8e-9f17-3d2b8a4c5e60");
    static constexpr std::string_view kComponentName = "test.position";
    float x = 0;
    float y = 0;
};

constexpr std::string_view kProgram = R"(module game

import rawframe.log
import rawframe.world
import std.io

struct Position {
    x: f32
    y: f32
}

fn tell(count: i32, positions: [Position], entities: [world.Entity]) {
    log.info("told")
    log.debug("left out at info")
    log.about(log.WARNING, entities[0], "about the first")
    io.write("one ")
    io.print("line")
    io.print("two\nlines")
    let i = 0
    while i < 3 {
        log.error("again")
        i = i + 1
    }
}

fn flood(count: i32, positions: [Position], entities: [world.Entity]) {
    let i = 0
    while i < 40 {
        if i % 2 == 0 {
            log.info("even")
        } else {
            log.info("odd")
        }
        i = i + 1
    }
}

fn wrong(count: i32, positions: [Position], entities: [world.Entity]) {
    log.info("before")
    log.about(7, entities[0], "no such level")
}
)";

std::shared_ptr<const kest::Program> program() {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "game.kest", .text = std::string{kProgram}}};
    std::string report;
    auto compiled = kest_library::compile("game.kest", kFiles, {}, &report);
    if (!compiled.has_value()) {
        std::fputs(report.c_str(), stderr);
    }
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

constexpr std::array<KestColumn, 2> kColumns = {
    KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write},
    KestColumn{.component = {}, .element = {}, .entities = true}};

/// One Position, `entry` run over it once a tick with `log` staging, its
/// records through `emitter`.
struct Run {
    std::shared_ptr<const schema::SchemaRegistry> registry;
    std::unique_ptr<world::World> world;
    std::unique_ptr<KestSystems> systems;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;
    diagnostics::Emitter emitter;

    Run(GameLog& log, std::string_view entry, diagnostics::Emitter to) : emitter(to) {
        schema::RegistryBuilder builder;
        builder.add(schema::describeComponent<Position>());
        registry = *builder.freeze();
        world = std::make_unique<world::World>(registry);
        const world::EntityHandle kEntity = *world->create();
        RAWFRAME_EXPECT(world->insert(kEntity, *registry->key<Position>(), Position{}).has_value());
        kest::DoorTable table;
        RAWFRAME_EXPECT(log.addDoors(table).has_value());
        const std::array<KestStaging*, 1> kStaging = {&log};
        const std::array<KestSystemDeclaration, 1> kDeclarations = {
            KestSystemDeclaration{.identity = "game.system", .entry = entry, .columns = kColumns}};
        auto made = KestSystems::create({.program = program(),
                                         .doors = std::move(table),
                                         .limits = {.heapBytes = 1U << 20U, .fuelPerCall = 100'000},
                                         .systems = kDeclarations,
                                         .staging = kStaging});
        RAWFRAME_EXPECT(made.has_value());
        systems = std::move(*made);
        std::vector<world::SystemDeclaration> declared;
        RAWFRAME_EXPECT(systems->declareSystems(*registry, declared).has_value());
        schedule.emplace(*world::Schedule::compile(declared, *registry));
    }

    /// Whether the tick's system succeeded.
    bool once() {
        const auto kReport = schedule->runTick(*world, tick, *world::TickRate::of(60), emitter);
        return kReport.has_value() && kReport->failures.empty();
    }
};

LogSettings settingsOf(std::string_view text) {
    const auto kConfiguration = composition::Configuration::parse(text);
    RAWFRAME_EXPECT(kConfiguration.has_value());
    auto settings = readLogSettings(*kConfiguration);
    RAWFRAME_EXPECT(settings.has_value());
    return settings.has_value() ? *settings : LogSettings{};
}

/// Says why, for a refusal the test did not expect.
void tell(const result::Error& error) {
    std::fprintf(stderr, "%s\n", std::string{error.description()}.c_str());
    for (const auto& field : error.context()) {
        std::fprintf(stderr, "  %s: %s\n", std::string{field.key}.c_str(), std::string{field.value}.c_str());
    }
}

bool refused(std::string_view text) {
    const auto kConfiguration = composition::Configuration::parse(text);
    return kConfiguration.has_value() && !readLogSettings(*kConfiguration).has_value();
}

} // namespace

RAWFRAME_TEST(ASystemsLogRecordsNameItAndTheTick) {
    Logged kept;
    GameLog log{LogSettings{}};
    Run run{log, "tell", kept.router.emitter()};
    RAWFRAME_EXPECT(run.once() && run.once());
    const std::vector<Kept> kWritten = kept.keeper.written();
    // Each tick: told, about the first, one line, two, lines, again; the
    // debug record is below info, and the repeats of `again` are counted.
    RAWFRAME_EXPECT(kWritten.size() == 12);
    if (kWritten.size() == 12) {
        RAWFRAME_EXPECT(kWritten[0].text == "told" && kWritten[0].system == "game.system" && kWritten[0].tick == 0 &&
                        kWritten[0].severity == diagnostics::Severity::Info);
        RAWFRAME_EXPECT(kWritten[1].text == "about the first" && kWritten[1].aboutEntity &&
                        kWritten[1].severity == diagnostics::Severity::Warning);
        RAWFRAME_EXPECT(kWritten[2].text == "one line" && kWritten[3].text == "two" && kWritten[4].text == "lines");
        RAWFRAME_EXPECT(kWritten[5].text == "again" && kWritten[5].severity == diagnostics::Severity::Error);
        RAWFRAME_EXPECT(kWritten[6].text == "told" && kWritten[6].tick == 1);
    }
    std::size_t held = 0;
    for (const Kept& each : kept.keeper.records) {
        if (each.code == "game_log_held") {
            ++held;
            RAWFRAME_EXPECT(each.repeated == 2 && each.overBound == 0 && each.system == "game.system" &&
                            each.severity == diagnostics::Severity::Warning);
        }
    }
    RAWFRAME_EXPECT(held == 2 && log.written() == 12 && log.held() == 4);
}

RAWFRAME_TEST(ARunWritesAtMostItsBoundOfRecords) {
    Logged kept;
    GameLog log{LogSettings{}};
    Run run{log, "flood", kept.router.emitter()};
    RAWFRAME_EXPECT(run.once());
    RAWFRAME_EXPECT(kept.keeper.written().size() == kMaximumLogRecordsPerRun);
    const Kept& told = kept.keeper.records.back();
    RAWFRAME_EXPECT(told.code == "game_log_held" && told.overBound == 8 && told.repeated == 0);
    // The next run starts counting again.
    RAWFRAME_EXPECT(run.once() && kept.keeper.written().size() == 2 * kMaximumLogRecordsPerRun);
}

RAWFRAME_TEST(AFailedRunsRecordsStillStand) {
    Logged kept;
    GameLog log{LogSettings{}};
    Run run{log, "wrong", kept.router.emitter()};
    // A level that is not one fails the run; what it wrote before stands.
    RAWFRAME_EXPECT(!run.once());
    const std::vector<Kept> kWritten = kept.keeper.written();
    RAWFRAME_EXPECT(kWritten.size() == 1 && kWritten[0].text == "before");
}

RAWFRAME_TEST(ConfigurationLeavesOutALevelOrASystem) {
    {
        Logged kept;
        GameLog log{settingsOf("kest.log_level = debug\n")};
        Run run{log, "tell", kept.router.emitter()};
        RAWFRAME_EXPECT(run.once() && kept.keeper.written().size() == 7 &&
                        kept.keeper.written()[1].text == "left out at info");
    }
    {
        Logged kept;
        GameLog log{settingsOf("kest.log_level = error\n")};
        Run run{log, "tell", kept.router.emitter()};
        RAWFRAME_EXPECT(run.once() && kept.keeper.written().size() == 1);
    }
    {
        Logged kept;
        GameLog log{settingsOf("kest.log_mute = other.system  game.system\n")};
        Run run{log, "tell", kept.router.emitter()};
        RAWFRAME_EXPECT(run.once() && kept.keeper.records.empty() && log.written() == 0);
    }
    RAWFRAME_EXPECT(settingsOf("").least == diagnostics::Severity::Info && settingsOf("").muted.empty());
    RAWFRAME_EXPECT(refused("kest.log_level = loud\n"));
    std::string many = "kest.log_mute =";
    for (std::size_t index = 0; index <= kMaximumMutedSystems; ++index) {
        many += ' ';
        many += static_cast<char>('a' + (index / 26));
        many += static_cast<char>('a' + (index % 26));
    }
    RAWFRAME_EXPECT(refused(many + "\n"));
}

RAWFRAME_TEST(AMessageIsCutOnAWholeCharacter) {
    Logged kept;
    GameLog log{LogSettings{}};
    schema::RegistryBuilder builder;
    const auto kRegistry = *builder.freeze();
    world::World world{kRegistry};
    world::CommandBuffer commands;
    world::SystemContext context{.world = world,
                                 .commands = commands,
                                 .tick = world::TickIndex{5},
                                 .rate = *world::TickRate::of(60),
                                 .emitter = kept.router.emitter(),
                                 .system = "game.direct",
                                 .declaredStreams = {}};
    // A two-byte character straddles the bound: it is left out whole.
    std::string message(kMaximumLogBytes - 1, 'a');
    message += "\xC3\xA9tail";
    log.begin(context);
    log.write(diagnostics::Severity::Warning, message, nullptr);
    log.end(false);
    const std::vector<Kept> kWritten = kept.keeper.written();
    RAWFRAME_EXPECT(kWritten.size() == 1 && kWritten[0].text == std::string(kMaximumLogBytes - 1, 'a') &&
                    kWritten[0].tick == 5 && kWritten[0].system == "game.direct");
    // Outside a run, nothing is written anywhere.
    log.write(diagnostics::Severity::Error, "after", nullptr);
    RAWFRAME_EXPECT(kept.keeper.written().size() == 1);
}

namespace {

constexpr auto kViewId = schema::ComponentTypeId::fromText("6a0d3e1f-0001-4c8e-9f17-3d2b8a4c5e60");
constexpr auto kPaceId = schema::ComponentTypeId::fromText("6a0d3e1f-0002-4c8e-9f17-3d2b8a4c5e60");

struct View {
    float x = 0;
};

constexpr std::string_view kShown = R"(module shown

import rawframe.log
import rawframe.world

struct View {
    x: f32
}

struct Pace {
    x: f32
}

fn show(count: i32, views: [View]) {
    log.info("shown")
}

fn step(count: i32, paces: [Pace]) {
    log.info("stepped")
    paces[0].x = paces[0].x + 1.0
}
)";

constexpr std::string_view kShownGame = "program shown.kest\n"
                                        "component 6a0d3e1f-0001-4c8e-9f17-3d2b8a4c5e60 shown.view View\n"
                                        "presentation shown.view on player\n"
                                        "present shown.show show write shown.view\n"
                                        "component 6a0d3e1f-0002-4c8e-9f17-3d2b8a4c5e60 shown.pace Pace\n"
                                        "player shown.pace\n"
                                        "input shown.pace\n"
                                        "system shown.step simulation step write shown.pace predicted\n";

} // namespace

RAWFRAME_TEST(AClientsPresentSystemsLogAndItsPredictorBindsQuietly) {
    const auto kFiles = GameFiles::fromHeld(
        "shown.game", {{"shown.game", std::string{kShownGame}}, {"shown.kest", std::string{kShown}}});
    RAWFRAME_EXPECT(kFiles.has_value());
    if (!kFiles.has_value()) {
        tell(kFiles.error());
        return;
    }
    std::string report;
    const auto kCompiled = kFiles->compile("shown.kest", {}, &report);
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        std::fprintf(stderr, "%s\n", report.c_str());
        return;
    }
    const std::vector<schema::ComponentDescriptor> kDescriptors = {
        {.id = kViewId, .name = "shown.view", .size = sizeof(View), .alignment = alignof(View), .plainData = true},
        {.id = kPaceId, .name = "shown.pace", .size = sizeof(View), .alignment = alignof(View), .plainData = true}};
    const kest::MachineLimits kLimits{.heapBytes = std::size_t{1} << 20U, .fuelPerCall = 100'000};
    auto presentation = ClientPresentation::create(PresentationSettings{
        .program = *kCompiled, .game = &kFiles->description(), .descriptors = kDescriptors, .limits = kLimits});
    if (!presentation.has_value() || *presentation == nullptr) {
        RAWFRAME_EXPECT(false);
        return;
    }
    schema::RegistryBuilder builder;
    // rawframe.world's persistent identity, which a server's World
    // registers itself, beside the game's.
    std::vector<schema::ComponentDescriptor> registered = kDescriptors;
    registered.push_back({.id = world::Persistent::kComponentTypeId,
                          .name = world::Persistent::kComponentName,
                          .size = sizeof(world::Persistent),
                          .alignment = alignof(world::Persistent),
                          .plainData = true});
    for (const schema::ComponentDescriptor& descriptor : registered) {
        builder.add(descriptor);
    }
    const auto kRegistry = *builder.freeze();
    world::World mirror{kRegistry};
    const world::EntityHandle kPlayer = *mirror.create();
    Logged kept;
    const auto kPresented =
        (*presentation)->present(mirror, kPlayer, {}, *world::TickRate::of(60), kept.router.emitter());
    RAWFRAME_EXPECT(kPresented.has_value());
    if (!kPresented.has_value()) {
        tell(kPresented.error());
    }
    const std::vector<Kept> kWritten = kept.keeper.written();
    RAWFRAME_EXPECT(kWritten.size() == 1 && kWritten[0].text == "shown" && kWritten[0].system == "shown.show");
    // The predictor runs the same program, so it binds the same doors.
    auto predictor = makePredictor(PredictorSettings{
        .program = *kCompiled, .game = &kFiles->description(), .descriptors = registered, .limits = kLimits});
    RAWFRAME_EXPECT(predictor.has_value());
    if (!predictor.has_value()) {
        tell(predictor.error());
    }
}
