// A Kest system looking up components on entities it does not run over
// (D391): a declared lookup reads another entity's value and is ordered as
// a read; an entity without the component is answered by `has`; a system
// that never declared the lookup, a `get` of what an entity lacks, and a
// lookup of a component the system writes are refused.

#include "rawframe/kest_library/library.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/kest_systems.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe;
using world_kest::KestColumn;
using world_kest::KestComponent;
using world_kest::KestSystemDeclaration;
using world_kest::KestSystems;

namespace {

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("4c0f6a1e-91d2-4b37-8e5a-0b7d3c2f1a90");
    static constexpr std::string_view kComponentName = "test.position";
    float x = 0;
    float y = 0;
};

/// The entity a follower watches.
struct Target {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("9e3b7d25-6a18-4f0c-b2d4-5c81e0a7f346");
    static constexpr std::string_view kComponentName = "test.target";
    world::EntityHandle entity;
};

/// What a follower saw of its target.
struct Seen {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("27d8c4f1-0b5e-4a69-93c7-e6f2a1b8d054");
    static constexpr std::string_view kComponentName = "test.seen";
    float x = 0;
    std::uint32_t found = 0;
};

constexpr std::string_view kProgram = R"(module look

import rawframe.world

struct Position {
    x: f32
    y: f32
}

struct Target {
    entity: world.Entity
}

struct Seen {
    x: f32
    found: u32
}

extern fn Position.get(entity: world.Entity) -> Position
extern fn Position.has(entity: world.Entity) -> bool

fn follow(count: i32, targets: [Target], seens: [Seen]) {
    let i = 0
    while i < count {
        seens[i].found = u32(0)
        if Position.has(targets[i].entity) {
            seens[i].x = Position.get(targets[i].entity).x
            seens[i].found = u32(1)
        }
        i = i + 1
    }
}

fn grab(count: i32, targets: [Target], seens: [Seen]) {
    let i = 0
    while i < count {
        seens[i].x = Position.get(targets[i].entity).x
        i = i + 1
    }
}

fn shift(count: i32, positions: [Position]) {
    let i = 0
    while i < count {
        positions[i].x = positions[i].x + 1.0
        i = i + 1
    }
}
)";

constexpr kest::MachineLimits kLimits{.heapBytes = 1U << 20U, .fuelPerCall = 1'000'000};

std::shared_ptr<const kest::Program> program() {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "look.kest", .text = std::string{kProgram}}};
    auto compiled = kest_library::compile("look.kest", kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(schema::describeComponent<Position>());
    builder.add(schema::describeComponent<Target>());
    builder.add(schema::describeComponent<Seen>());
    return *builder.freeze();
}

constexpr std::array<KestComponent, 1> kComponents = {
    KestComponent{.component = Position::kComponentTypeId, .kestType = "Position"}};

constexpr std::array<KestColumn, 2> kFollower = {
    KestColumn{.component = Target::kComponentTypeId, .element = "Target", .access = world::Access::Read},
    KestColumn{.component = Seen::kComponentTypeId, .element = "Seen", .access = world::Access::Write}};

constexpr std::array<KestColumn, 1> kMover = {
    KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write}};

constexpr std::array<schema::ComponentTypeId, 1> kPositions = {Position::kComponentTypeId};

/// The systems declared over `registry` and compiled into a schedule, or
/// nothing when the declaration is refused.
std::optional<world::Schedule> scheduled(std::span<const KestSystemDeclaration> declarations,
                                         const schema::SchemaRegistry& registry,
                                         std::unique_ptr<KestSystems>& kest,
                                         std::vector<world::SystemDeclaration>& declared) {
    auto made = KestSystems::create(
        {.program = program(), .components = kComponents, .limits = kLimits, .systems = declarations});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return std::nullopt;
    }
    kest = std::move(*made);
    if (!kest->declareSystems(registry, declared).has_value()) {
        return std::nullopt;
    }
    auto compiled = world::Schedule::compile(declared, registry);
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? std::optional{std::move(*compiled)} : std::nullopt;
}

} // namespace

RAWFRAME_TEST(ASystemLooksUpWhatItDeclares) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const auto kPosition = *kRegistry->key<Position>();
    const auto kTarget = *kRegistry->key<Target>();
    const auto kSeen = *kRegistry->key<Seen>();
    const world::EntityHandle kLeader = *world.create();
    RAWFRAME_EXPECT(world.insert(kLeader, kPosition, Position{7, 0}).has_value());
    const world::EntityHandle kBare = *world.create();
    // One follower watches the leader, another an entity with no position.
    const world::EntityHandle kWatcher = *world.create();
    RAWFRAME_EXPECT(world.insert(kWatcher, kTarget, Target{kLeader}).has_value());
    RAWFRAME_EXPECT(world.insert(kWatcher, kSeen, Seen{}).has_value());
    const world::EntityHandle kLost = *world.create();
    RAWFRAME_EXPECT(world.insert(kLost, kTarget, Target{kBare}).has_value());
    RAWFRAME_EXPECT(world.insert(kLost, kSeen, Seen{.x = 3}).has_value());

    // The mover runs first, so the follower sees where it moved the leader.
    const std::array<std::string_view, 1> kAfter = {"look.shift"};
    const std::array<KestSystemDeclaration, 2> kDeclarations = {
        KestSystemDeclaration{.identity = "look.shift", .entry = "shift", .columns = kMover},
        KestSystemDeclaration{.identity = "look.follow",
                              .entry = "follow",
                              .columns = kFollower,
                              .after = kAfter,
                              .lookups = kPositions}};
    std::unique_ptr<KestSystems> kest;
    std::vector<world::SystemDeclaration> declared;
    auto ticks = scheduled(kDeclarations, *kRegistry, kest, declared);
    RAWFRAME_EXPECT(ticks.has_value());
    if (!ticks.has_value()) {
        return;
    }
    // Its lookup is ordered as a read of positions.
    RAWFRAME_EXPECT(declared.size() == 2 && std::ranges::contains(declared[1].reads, kPosition.id));
    world::TickIndex tick;
    const auto kReport = ticks->runTick(world, tick, *world::TickRate::of(60));
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty());
    RAWFRAME_EXPECT(world.get(kWatcher, kSeen)->x == 8 && world.get(kWatcher, kSeen)->found == 1);
    RAWFRAME_EXPECT(world.get(kLost, kSeen)->x == 3 && world.get(kLost, kSeen)->found == 0);
}

RAWFRAME_TEST(LookupsOutsideTheDeclarationAreRefused) {
    const auto kRegistry = registry();
    world::World world{kRegistry};
    const auto kPosition = *kRegistry->key<Position>();
    const auto kTarget = *kRegistry->key<Target>();
    const auto kSeen = *kRegistry->key<Seen>();
    const world::EntityHandle kLeader = *world.create();
    RAWFRAME_EXPECT(world.insert(kLeader, kPosition, Position{7, 0}).has_value());
    const world::EntityHandle kWatcher = *world.create();
    RAWFRAME_EXPECT(world.insert(kWatcher, kTarget, Target{kLeader}).has_value());
    RAWFRAME_EXPECT(world.insert(kWatcher, kSeen, Seen{}).has_value());

    // Undeclared, the same `get` fails the system, which changes nothing.
    const std::array<KestSystemDeclaration, 1> kUndeclared = {
        KestSystemDeclaration{.identity = "look.grab", .entry = "grab", .columns = kFollower}};
    std::unique_ptr<KestSystems> kest;
    std::vector<world::SystemDeclaration> declared;
    auto ticks = scheduled(kUndeclared, *kRegistry, kest, declared);
    RAWFRAME_EXPECT(ticks.has_value());
    world::TickIndex tick;
    if (ticks.has_value()) {
        const auto kReport = ticks->runTick(world, tick, *world::TickRate::of(60));
        RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.size() == 1);
        RAWFRAME_EXPECT(world.get(kWatcher, kSeen)->x == 0);
    }

    // Declared, a `get` of an entity without the component fails too.
    RAWFRAME_EXPECT(world.remove(kLeader, kPosition).has_value());
    const std::array<KestSystemDeclaration, 1> kDeclared = {
        KestSystemDeclaration{.identity = "look.grab", .entry = "grab", .columns = kFollower, .lookups = kPositions}};
    declared.clear();
    ticks = scheduled(kDeclared, *kRegistry, kest, declared);
    RAWFRAME_EXPECT(ticks.has_value());
    if (ticks.has_value()) {
        const auto kReport = ticks->runTick(world, tick, *world::TickRate::of(60));
        RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.size() == 1);
    }

    // A system may not look up what it writes: its writes wait in its
    // journal, and the lookup would read past them.
    const std::array<KestSystemDeclaration, 1> kWritten = {
        KestSystemDeclaration{.identity = "look.shift", .entry = "shift", .columns = kMover, .lookups = kPositions}};
    declared.clear();
    RAWFRAME_EXPECT(!scheduled(kWritten, *kRegistry, kest, declared).has_value());
}
