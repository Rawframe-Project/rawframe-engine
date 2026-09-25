// Kest systems over World columns: run per archetype, journaled writes that
// land only when every call succeeds, and shapes checked before anything runs.

#include "rawframe/kest/errors.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace rawframe;
using world_kest::KestColumn;
using world_kest::KestSystemDeclaration;
using world_kest::KestSystems;

namespace {

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1");
    static constexpr std::string_view kComponentName = "test.position";
    float x = 0;
    float y = 0;
};

struct Velocity {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8");
    static constexpr std::string_view kComponentName = "test.velocity";
    float dx = 0;
    float dy = 0;
};

/// Only here to put some movers in a second archetype.
struct Marked {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("a7e40f19-2d86-4c51-b3f8-6e0c9d15a273");
    static constexpr std::string_view kComponentName = "test.marked";
    std::int32_t mark = 0;
};

/// Not plain data, so never lent.
struct Named {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("c2f5b8d4-1e93-4a07-86bd-3f40e9a1c6d2");
    static constexpr std::string_view kComponentName = "test.named";
    std::string name;
};

constexpr std::string_view kProgram = "module game\n"
                                      "\n"
                                      "struct Position {\n"
                                      "    x: f32\n"
                                      "    y: f32\n"
                                      "}\n"
                                      "\n"
                                      "struct Velocity {\n"
                                      "    dx: f32\n"
                                      "    dy: f32\n"
                                      "}\n"
                                      "\n"
                                      "struct Wide {\n"
                                      "    a: f64\n"
                                      "    b: f64\n"
                                      "}\n"
                                      "\n"
                                      "fn integrate(count: i32, positions: [Position], velocities: [Velocity]) {\n"
                                      "    let i = 0\n"
                                      "    while i < count {\n"
                                      "        positions[i].x = positions[i].x + velocities[i].dx\n"
                                      "        positions[i].y = positions[i].y + velocities[i].dy\n"
                                      "        i = i + 1\n"
                                      "    }\n"
                                      "}\n"
                                      "\n"
                                      "fn fragile(count: i32, positions: [Position]) {\n"
                                      "    let i = 0\n"
                                      "    while i < count {\n"
                                      "        positions[i].x = positions[i].x + 1.0\n"
                                      "        i = i + 1\n"
                                      "    }\n"
                                      "    if positions[0].x > 100.0 {\n"
                                      "        positions[count].x = 0.0\n"
                                      "    }\n"
                                      "}\n"
                                      "\n"
                                      "fn wide(count: i32, values: [Wide]) {\n"
                                      "}\n";

constexpr kest::MachineLimits kLimits{.heapBytes = 1U << 20U, .fuelPerCall = 1'000'000};

std::shared_ptr<const kest::Program> program() {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "game.kest", .text = std::string{kProgram}}};
    auto compiled = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(schema::describeComponent<Position>());
    builder.add(schema::describeComponent<Velocity>());
    builder.add(schema::describeComponent<Marked>());
    builder.add(schema::describeComponent<Named>());
    return *builder.freeze();
}

constexpr std::array<KestColumn, 2> kMotion = {
    KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write},
    KestColumn{.component = Velocity::kComponentTypeId, .element = "Velocity", .access = world::Access::Read}};

constexpr std::array<KestColumn, 1> kFragile = {
    KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write}};

struct Movers {
    std::vector<world::EntityHandle> plain;
    std::vector<world::EntityHandle> marked;
};

/// Three movers in one archetype, then two in a second.
Movers spawn(world::World& world, float markedX = 50) {
    Movers movers;
    const auto kPosition = *world.registry().key<Position>();
    const auto kVelocity = *world.registry().key<Velocity>();
    const auto kMarked = *world.registry().key<Marked>();
    for (int index = 0; index < 5; ++index) {
        const world::EntityHandle kEntity = *world.create();
        const bool kMarkedOne = index >= 3;
        RAWFRAME_EXPECT(world.insert(kEntity, kPosition, Position{kMarkedOne ? markedX : static_cast<float>(index), 0})
                            .has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, kVelocity, Velocity{1, 2}).has_value());
        if (kMarkedOne) {
            RAWFRAME_EXPECT(world.insert(kEntity, kMarked, Marked{index}).has_value());
            movers.marked.push_back(kEntity);
        } else {
            movers.plain.push_back(kEntity);
        }
    }
    return movers;
}

std::unique_ptr<KestSystems> systems(std::span<const KestSystemDeclaration> declarations) {
    auto made = KestSystems::create({.program = program(), .limits = kLimits, .systems = declarations});
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? std::move(*made) : nullptr;
}

world::Schedule schedule(KestSystems& kest, const schema::SchemaRegistry& registry) {
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(kest.declareSystems(registry, declarations).has_value());
    auto compiled = world::Schedule::compile(declarations, registry);
    RAWFRAME_EXPECT(compiled.has_value());
    return std::move(*compiled);
}

} // namespace

RAWFRAME_TEST(AKestSystemMovesEveryMatchingArchetype) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const Movers kMovers = spawn(world);
    const std::array<KestSystemDeclaration, 1> kDeclarations = {
        KestSystemDeclaration{.identity = "game.integrate", .entry = "integrate", .columns = kMotion}};
    auto kest = systems(kDeclarations);
    world::Schedule ticks = schedule(*kest, *kRegistry);
    world::TickIndex tick;
    for (int round = 0; round < 3; ++round) {
        auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
        RAWFRAME_EXPECT(report.has_value() && report->failures.empty());
    }
    const auto kPosition = *world.registry().key<Position>();
    RAWFRAME_EXPECT(world.get(kMovers.plain[0], kPosition)->x == 3);
    RAWFRAME_EXPECT(world.get(kMovers.plain[2], kPosition)->x == 5);
    RAWFRAME_EXPECT(world.get(kMovers.plain[2], kPosition)->y == 6);
    RAWFRAME_EXPECT(world.get(kMovers.marked[1], kPosition)->x == 53);
}

RAWFRAME_TEST(ARefusedSystemChangesNothing) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    // The marked archetype runs second and refuses; the first archetype's
    // writes, already made in the journal, must not land either.
    const Movers kMovers = spawn(world, 500);
    const std::array<KestSystemDeclaration, 1> kDeclarations = {
        KestSystemDeclaration{.identity = "game.fragile", .entry = "fragile", .columns = kFragile}};
    auto kest = systems(kDeclarations);
    world::Schedule ticks = schedule(*kest, *kRegistry);
    world::TickIndex tick;
    auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
    RAWFRAME_EXPECT(report.has_value() && report->failures.size() == 1);
    RAWFRAME_EXPECT(report.has_value() && report->failures[0].system == "game.fragile");
    const auto kPosition = *world.registry().key<Position>();
    RAWFRAME_EXPECT(world.get(kMovers.plain[1], kPosition)->x == 1);
    RAWFRAME_EXPECT(world.get(kMovers.marked[0], kPosition)->x == 500);
    // And the machine is fine: the next tick refuses the same way, no worse.
    report = ticks.runTick(world, tick, *world::TickRate::of(60));
    RAWFRAME_EXPECT(report.has_value() && report->failures.size() == 1);
    RAWFRAME_EXPECT(world.get(kMovers.plain[1], kPosition)->x == 1);
}

RAWFRAME_TEST(AKestSystemDeclaresItsAccess) {
    const auto kRegistry = registry();
    const std::array<KestSystemDeclaration, 1> kDeclarations = {
        KestSystemDeclaration{.identity = "game.integrate", .entry = "integrate", .columns = kMotion}};
    auto kest = systems(kDeclarations);
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(kest->declareSystems(*kRegistry, declarations).has_value());
    RAWFRAME_EXPECT(declarations.size() == 1);
    const auto kPosition = *kRegistry->key<Position>();
    const auto kVelocity = *kRegistry->key<Velocity>();
    RAWFRAME_EXPECT(declarations[0].writes.size() == 1 && declarations[0].writes[0] == kPosition.id);
    RAWFRAME_EXPECT(declarations[0].reads.size() == 1 && declarations[0].reads[0] == kVelocity.id);
}

RAWFRAME_TEST(ShapesAreCheckedBeforeAnythingRuns) {
    const auto kRegistry = registry();
    const auto kRefusedAtCreate = [](std::span<const KestSystemDeclaration> declarations) {
        return !KestSystems::create({.program = program(), .limits = kLimits, .systems = declarations}).has_value();
    };
    // An entry taking fewer arrays than the columns lend.
    const std::array<KestSystemDeclaration, 1> kShort = {
        KestSystemDeclaration{.identity = "game.fragile", .entry = "fragile", .columns = kMotion}};
    RAWFRAME_EXPECT(kRefusedAtCreate(kShort));
    const std::array<KestSystemDeclaration, 1> kNoEntry = {
        KestSystemDeclaration{.identity = "game.none", .entry = "absent", .columns = kFragile}};
    RAWFRAME_EXPECT(kRefusedAtCreate(kNoEntry));
    constexpr std::array<KestColumn, 1> kUnknownType = {
        KestColumn{.component = Position::kComponentTypeId, .element = "Absent", .access = world::Access::Write}};
    const std::array<KestSystemDeclaration, 1> kNoType = {
        KestSystemDeclaration{.identity = "game.fragile", .entry = "fragile", .columns = kUnknownType}};
    RAWFRAME_EXPECT(kRefusedAtCreate(kNoType));

    const auto kRefusedAtDeclare = [&kRegistry](std::span<const KestColumn> columns, std::string_view entry) {
        const std::array<KestSystemDeclaration, 1> kDeclarations = {
            KestSystemDeclaration{.identity = "game.check", .entry = entry, .columns = columns}};
        auto kest = KestSystems::create({.program = program(), .limits = kLimits, .systems = kDeclarations});
        RAWFRAME_EXPECT(kest.has_value());
        std::vector<world::SystemDeclaration> declarations;
        auto declared = (*kest)->declareSystems(*kRegistry, declarations);
        return !declared.has_value() &&
               declared.error().code() == world_kest::code(world_kest::WorldKestError::ColumnMismatch);
    };
    // A Kest type of another size.
    constexpr std::array<KestColumn, 1> kWide = {
        KestColumn{.component = Position::kComponentTypeId, .element = "Wide", .access = world::Access::Write}};
    RAWFRAME_EXPECT(kRefusedAtDeclare(kWide, "wide"));
    // A component that is not plain data.
    constexpr std::array<KestColumn, 1> kNamed = {
        KestColumn{.component = Named::kComponentTypeId, .element = "Wide", .access = world::Access::Write}};
    RAWFRAME_EXPECT(kRefusedAtDeclare(kNamed, "wide"));
}

namespace {

constexpr std::string_view kKeeper = "module keeper\n"
                                     "\n"
                                     "import rawframe.world\n"
                                     "\n"
                                     "struct Link {\n"
                                     "    target: world.Entity\n"
                                     "}\n"
                                     "\n"
                                     "fn make(count: i32, links: [Link]) {\n"
                                     "    let i = 0\n"
                                     "    while i < count {\n"
                                     "        links[i] = Link(world.create())\n"
                                     "        i = i + 1\n"
                                     "    }\n"
                                     "}\n"
                                     "\n"
                                     "fn kill(count: i32, links: [Link]) {\n"
                                     "    let i = 0\n"
                                     "    while i < count {\n"
                                     "        world.destroy(links[i].target)\n"
                                     "        i = i + 1\n"
                                     "    }\n"
                                     "}\n";

std::string readText(const std::string& path) {
    std::string text;
    if (std::FILE* file = std::fopen(path.c_str(), "rb")) {
        char chunk[4096];
        std::size_t got = 0;
        while ((got = std::fread(chunk, 1, sizeof chunk, file)) != 0) {
            text.append(chunk, got);
        }
        std::fclose(file);
    }
    return text;
}

} // namespace

RAWFRAME_TEST(AnEntityMadeInOneRunIsNotTakenInAnother) {
    const std::array<kest::SourceFile, 2> kFiles = {
        kest::SourceFile{.path = "keeper.kest", .text = std::string{kKeeper}},
        kest::SourceFile{.path = "rawframe/world.kest",
                         .text = readText(std::string{RAWFRAME_WORLD_KEST_MODULES} + "rawframe/world.kest")}};
    auto compiled = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    if (!compiled.has_value()) {
        return;
    }
    constexpr auto kLinkId = schema::ComponentTypeId::fromText("e4b7c1d9-2a36-4f58-9b0e-5c71d3a8f262");
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{
        .id = kLinkId, .name = "test.link", .size = 8, .alignment = 4, .plainData = true, .operations = {}});
    const auto kRegistry = *builder.freeze();
    world::World world{kRegistry};
    const auto kLink = *kRegistry->find(kLinkId);
    std::array<std::uint32_t, 2> zero{};
    const world::EntityHandle kHolder = *world.create();
    RAWFRAME_EXPECT(world.insertErased(kHolder, kLink, zero.data()).has_value());

    constexpr std::array<KestColumn, 1> kLinks = {
        KestColumn{.component = kLinkId, .element = "Link", .access = world::Access::Write}};
    const std::array<KestSystemDeclaration, 2> kDeclarations = {
        KestSystemDeclaration{.identity = "keeper.make", .entry = "make", .columns = kLinks},
        KestSystemDeclaration{
            .identity = "keeper.kill", .phase = world::Phase::PostSimulation, .entry = "kill", .columns = kLinks}};
    auto kest = KestSystems::create({.program = *compiled, .limits = kLimits, .systems = kDeclarations});
    RAWFRAME_EXPECT(kest.has_value());
    if (!kest.has_value()) {
        return;
    }
    world::Schedule ticks = schedule(**kest, *kRegistry);
    world::TickIndex tick;
    auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
    // `make` stored an entity that exists only inside its own run; `kill`,
    // another run, is refused for naming it.
    RAWFRAME_EXPECT(report.has_value() && report->failures.size() == 1);
    RAWFRAME_EXPECT(report.has_value() && report->failures[0].system == "keeper.kill");
    RAWFRAME_EXPECT(report.has_value() &&
                    report->failures[0].error.description().find("another system run") != std::string_view::npos);
    // The creation itself still happened at the barrier.
    RAWFRAME_EXPECT(world.entityCount() == 2);
}

namespace {

constexpr std::string_view kDice = "module dice\n"
                                   "\n"
                                   "import rawframe.random\n"
                                   "\n"
                                   "struct Die {\n"
                                   "    face: u32\n"
                                   "    spin: f64\n"
                                   "}\n"
                                   "\n"
                                   "fn roll(count: i32, dice: [Die]) {\n"
                                   "    let i = 0\n"
                                   "    while i < count {\n"
                                   "        dice[i].face = random.below(0, 6) + 1\n"
                                   "        dice[i].spin = random.unit(1)\n"
                                   "        i = i + 1\n"
                                   "    }\n"
                                   "}\n"
                                   "\n"
                                   "fn cheat(count: i32, dice: [Die]) {\n"
                                   "    dice[0].face = random.below(2, 6)\n"
                                   "}\n";

struct Die {
    std::uint32_t face;
    double spin;
};

constexpr auto kDieId = schema::ComponentTypeId::fromText("b83e5f21-7d49-4c0a-a6e2-1f94c8d3b705");

/// Rolls three dice for five ticks under `seed` and returns every face and
/// spin, or nothing when a tick reports a failure.
std::vector<Die> roll(std::uint64_t seed, std::string_view entry) {
    const std::array<kest::SourceFile, 2> kFiles = {
        kest::SourceFile{.path = "dice.kest", .text = std::string{kDice}},
        kest::SourceFile{.path = "rawframe/random.kest",
                         .text = readText(std::string{RAWFRAME_WORLD_KEST_MODULES} + "rawframe/random.kest")}};
    auto compiled = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{
        .id = kDieId, .name = "test.die", .size = 16, .alignment = 8, .plainData = true, .operations = {}});
    const auto kRegistry = *builder.freeze();
    world::World world{kRegistry, world::WorldSettings{.rootSeed = world::RootSeed{seed}}};
    const auto kDie = *kRegistry->find(kDieId);
    for (int index = 0; index < 3; ++index) {
        Die die{};
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, kDie, &die).has_value());
    }
    constexpr std::array<KestColumn, 1> kDieColumns = {
        KestColumn{.component = kDieId, .element = "Die", .access = world::Access::Write}};
    constexpr std::array<std::string_view, 2> kStreams = {"faces", "spins"};
    const std::array<KestSystemDeclaration, 1> kDeclarations = {KestSystemDeclaration{
        .identity = "dice.roll", .entry = entry, .columns = kDieColumns, .randomStreams = kStreams}};
    auto kest = KestSystems::create({.program = *compiled, .limits = kLimits, .systems = kDeclarations});
    RAWFRAME_EXPECT(kest.has_value());
    world::Schedule ticks = schedule(**kest, *kRegistry);
    world::TickIndex tick;
    std::vector<Die> seen;
    for (int round = 0; round < 5; ++round) {
        auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
        if (!report.has_value() || !report->failures.empty()) {
            return {};
        }
        const auto kQuery = std::array<world::ColumnTerm, 1>{world::ColumnTerm{kDie, world::Access::Read}};
        auto query = world::ColumnQuery::resolve(kQuery, *kRegistry);
        query->forEachChunk(world, [&seen](const world::ColumnChunk& chunk) {
            const auto* values = reinterpret_cast<const Die*>(chunk.columns[0]);
            seen.insert(seen.end(), values, values + chunk.entities.size());
        });
    }
    return seen;
}

} // namespace

RAWFRAME_TEST(KestSystemsDrawFromTheWorldsStreams) {
    const std::vector<Die> kFirst = roll(1234, "roll");
    const std::vector<Die> kAgain = roll(1234, "roll");
    const std::vector<Die> kOther = roll(4321, "roll");
    RAWFRAME_EXPECT(kFirst.size() == 15 && kAgain.size() == 15 && kOther.size() == 15);
    bool same = kFirst.size() == kAgain.size();
    bool differs = false;
    bool inRange = true;
    for (std::size_t index = 0; index < kFirst.size() && index < kAgain.size() && index < kOther.size(); ++index) {
        same = same && kFirst[index].face == kAgain[index].face && kFirst[index].spin == kAgain[index].spin;
        differs = differs || kFirst[index].face != kOther[index].face || kFirst[index].spin != kOther[index].spin;
        inRange = inRange && kFirst[index].face >= 1 && kFirst[index].face <= 6 && kFirst[index].spin >= 0 &&
                  kFirst[index].spin < 1;
    }
    RAWFRAME_EXPECT(same && differs && inRange);
    // A stream the system did not declare fails the system.
    RAWFRAME_EXPECT(roll(1234, "cheat").empty());
}

namespace {

std::shared_ptr<const kest::Program> programFrom(std::string_view text) {
    const std::array<kest::SourceFile, 1> kFiles = {kest::SourceFile{.path = "game.kest", .text = std::string{text}}};
    auto compiled = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

/// The program with its movers going the other way, and nothing else changed.
std::string reversed() {
    std::string text{kProgram};
    const std::string kForward = "positions[i].x = positions[i].x + velocities[i].dx";
    text.replace(text.find(kForward), kForward.size(), "positions[i].x = positions[i].x - velocities[i].dx");
    return text;
}

} // namespace

RAWFRAME_TEST(AProgramReloadsAtomicallyKeepingTheWorld) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const Movers kMovers = spawn(world);
    const std::array<KestSystemDeclaration, 1> kDeclarations = {
        KestSystemDeclaration{.identity = "game.integrate", .entry = "integrate", .columns = kMotion}};
    auto kest = systems(kDeclarations);
    world::Schedule ticks = schedule(*kest, *kRegistry);
    world::TickIndex tick;
    const auto kPosition = *world.registry().key<Position>();
    const auto kTickOnce = [&] {
        auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
        RAWFRAME_EXPECT(report.has_value() && report->failures.empty());
    };
    kTickOnce();
    kTickOnce();
    RAWFRAME_EXPECT(world.get(kMovers.plain[0], kPosition)->x == 2);

    // Same shapes, other behaviour: the World and schedule stay, the code changes.
    RAWFRAME_EXPECT(kest->reload(programFrom(reversed())).has_value());
    kTickOnce();
    RAWFRAME_EXPECT(world.get(kMovers.plain[0], kPosition)->x == 1);

    // A program changing a shape the World holds, or losing an entry, is
    // refused and the running one carries on.
    std::string wider{kProgram};
    const std::string kLastField = "    y: f32\n}";
    wider.replace(wider.find(kLastField), kLastField.size(), "    y: f32\n    z: f32\n}");
    const auto kWider = kest->reload(programFrom(wider));
    RAWFRAME_EXPECT(!kWider.has_value() &&
                    kWider.error().code() == world_kest::code(world_kest::WorldKestError::ColumnMismatch));
    std::string renamed = reversed();
    const std::string kEntry = "fn integrate(";
    renamed.replace(renamed.find(kEntry), kEntry.size(), "fn integrated(");
    RAWFRAME_EXPECT(!kest->reload(programFrom(renamed)).has_value());
    kTickOnce();
    RAWFRAME_EXPECT(world.get(kMovers.plain[0], kPosition)->x == 0);
}

RAWFRAME_TEST(AnUntrustedProgramWritesItsColumnsAndOpensNoWorldDoor) {
    // A mod's handlers (D181): its columns lent and written as a game's are.
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const Movers kMovers = spawn(world);
    const std::array<KestSystemDeclaration, 1> kDeclarations = {
        KestSystemDeclaration{.identity = "mod.integrate", .entry = "integrate", .columns = kMotion}};
    auto kest = KestSystems::create(
        {.program = program(), .limits = kLimits, .trust = kest::Trust::Untrusted, .systems = kDeclarations});
    RAWFRAME_EXPECT(kest.has_value());
    if (!kest.has_value()) {
        return;
    }
    world::Schedule ticks = schedule(**kest, *kRegistry);
    world::TickIndex tick;
    RAWFRAME_EXPECT(ticks.runTick(world, tick, *world::TickRate::of(60)).has_value());
    RAWFRAME_EXPECT(world.get(kMovers.plain[0], *world.registry().key<Position>())->x == 1);

    // A program that makes entities needs World.create, which is not for
    // untrusted code: it does not start.
    const std::array<kest::SourceFile, 2> kFiles = {
        kest::SourceFile{.path = "keeper.kest", .text = std::string{kKeeper}},
        kest::SourceFile{.path = "rawframe/world.kest",
                         .text = readText(std::string{RAWFRAME_WORLD_KEST_MODULES} + "rawframe/world.kest")}};
    auto keeper = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(keeper.has_value());
    if (!keeper.has_value()) {
        return;
    }
    constexpr std::array<KestColumn, 1> kLinks = {
        KestColumn{.component = schema::ComponentTypeId::fromText("e4b7c1d9-2a36-4f58-9b0e-5c71d3a8f262"),
                   .element = "Link",
                   .access = world::Access::Write}};
    const std::array<KestSystemDeclaration, 1> kMake = {
        KestSystemDeclaration{.identity = "mod.make", .entry = "make", .columns = kLinks}};
    const auto kRefused =
        KestSystems::create({.program = *keeper, .limits = kLimits, .trust = kest::Trust::Untrusted, .systems = kMake});
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().domain() == kest::kKestDomain &&
                    kRefused.error().code() == code(kest::KestError::DoorNotForUntrusted));
    RAWFRAME_EXPECT(KestSystems::create({.program = *keeper, .limits = kLimits, .systems = kMake}).has_value());
}

namespace {

/// A mod's handlers as a hostile author writes them (D183): each writes
/// first, then never ends, reads past its array, or grows without bound.
constexpr std::string_view kHostileHandlers = "module mod\n"
                                              "\n"
                                              "struct Position {\n"
                                              "    x: f32\n"
                                              "    y: f32\n"
                                              "}\n"
                                              "\n"
                                              "fn spin(count: i32, positions: [Position]) {\n"
                                              "    positions[0].x = 99.0\n"
                                              "    let i = 0\n"
                                              "    while true {\n"
                                              "        i = i + 1\n"
                                              "    }\n"
                                              "}\n"
                                              "\n"
                                              "fn stray(count: i32, positions: [Position]) {\n"
                                              "    positions[0].x = 99.0\n"
                                              "    positions[count + 5].x = 1.0\n"
                                              "}\n"
                                              "\n"
                                              "fn hoard(count: i32, positions: [Position]) {\n"
                                              "    positions[0].x = 99.0\n"
                                              "    let xs = array(0, 0)\n"
                                              "    while true {\n"
                                              "        push(xs, 1)\n"
                                              "    }\n"
                                              "}\n"
                                              "\n"
                                              "fn nudge(count: i32, positions: [Position]) {\n"
                                              "    let i = 0\n"
                                              "    while i < count {\n"
                                              "        positions[i].y = positions[i].y + 1.0\n"
                                              "        i = i + 1\n"
                                              "    }\n"
                                              "}\n";

/// And one that calls itself without end, which Kest cannot bound.
constexpr std::string_view kBottomless = "module mod\n"
                                         "\n"
                                         "struct Position {\n"
                                         "    x: f32\n"
                                         "    y: f32\n"
                                         "}\n"
                                         "\n"
                                         "fn dive(count: i32, positions: [Position]) {\n"
                                         "    positions[0].x = 99.0\n"
                                         "    dive(count, positions)\n"
                                         "}\n";

std::shared_ptr<const kest::Program> compiledMod(std::string_view text) {
    const std::array<kest::SourceFile, 1> kFiles = {kest::SourceFile{.path = "mod.kest", .text = std::string{text}}};
    auto compiled = kest::Program::compile(kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

} // namespace

RAWFRAME_TEST(AHostileHandlerFailsAloneAndChangesNothing) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const Movers kMovers = spawn(world);
    const std::array<KestSystemDeclaration, 4> kDeclarations = {
        KestSystemDeclaration{.identity = "mod.spin", .entry = "spin", .columns = kFragile},
        KestSystemDeclaration{.identity = "mod.stray", .entry = "stray", .columns = kFragile},
        KestSystemDeclaration{.identity = "mod.hoard", .entry = "hoard", .columns = kFragile},
        KestSystemDeclaration{.identity = "mod.nudge", .entry = "nudge", .columns = kFragile}};
    auto kest = KestSystems::create({.program = compiledMod(kHostileHandlers),
                                     .limits = kLimits,
                                     .trust = kest::Trust::Untrusted,
                                     .systems = kDeclarations});
    RAWFRAME_EXPECT(kest.has_value());
    if (!kest.has_value()) {
        return;
    }
    world::Schedule ticks = schedule(**kest, *kRegistry);
    world::TickIndex tick;
    const auto kPosition = *world.registry().key<Position>();
    // Tick after tick, each hostile handler fails on its own, what it wrote
    // before failing never lands, and the one well-behaved handler runs.
    for (int round = 1; round <= 3; ++round) {
        auto report = ticks.runTick(world, tick, *world::TickRate::of(60));
        RAWFRAME_EXPECT(report.has_value() && report->failures.size() == 3);
        for (const world::EntityHandle kEntity : kMovers.plain) {
            RAWFRAME_EXPECT(world.get(kEntity, kPosition)->x != 99 &&
                            world.get(kEntity, kPosition)->y == static_cast<float>(round));
        }
    }
    // A handler that calls itself without end never starts.
    const std::array<KestSystemDeclaration, 1> kDive = {
        KestSystemDeclaration{.identity = "mod.dive", .entry = "dive", .columns = kFragile}};
    RAWFRAME_EXPECT(!KestSystems::create({.program = compiledMod(kBottomless),
                                          .limits = kLimits,
                                          .trust = kest::Trust::Untrusted,
                                          .systems = kDive})
                         .has_value());
}
