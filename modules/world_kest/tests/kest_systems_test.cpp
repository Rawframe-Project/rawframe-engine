// Kest systems over World columns: run per archetype, journaled writes that
// land only when every call succeeds, and shapes checked before anything runs.

#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
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
