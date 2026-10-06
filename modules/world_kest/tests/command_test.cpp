// A player's commands at the server's doors (D425): each is readable by the
// systems of the first tick at or after the one it was delivered for, and
// of that tick only, with the player who sent it; an index past the count
// fails the run; elsewhere none is read. A command type holding an entity,
// or too large, is refused.

#include "../src/command_doors.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/test/test.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("6a1e0c37-2f4d-4c1b-8a9e-5d7b3e2f1c40");
    static constexpr std::string_view kComponentName = "test.position";
    float x = 0;
    float y = 0;
};

constexpr std::string_view kProgram = R"(module game

import rawframe.world

struct Position {
    x: f32
    y: f32
}

struct Push {
    by: f32
}

struct Pointing {
    at: world.Entity
}

struct Huge {
    a: [f64; 200]
}

extern fn CommandCount.push() -> i32
extern fn Command.push(index: i32) -> Push
extern fn CommandFrom.push(index: i32) -> world.Entity

// Moves each player by what it asked; refuses when asked for the place
// past the last command.
fn obey(count: i32, positions: [Position], entities: [world.Entity]) {
    let asks = CommandCount.push()
    let a = 0
    while a < asks {
        let from = CommandFrom.push(a)
        let i = 0
        while i < count {
            if entities[i] == from {
                positions[i].x = positions[i].x + Command.push(a).by
            }
            i = i + 1
        }
        a = a + 1
    }
    if positions[0].x > 100.0 {
        let past = Command.push(asks)
    }
}

fn unused(pointing: Pointing, huge: Huge) -> f32 {
    return 0.0
}
)";

GameDescription game(std::string_view type = "Push") {
    GameDescription made;
    made.commands.push_back(GameCommand{.name = "push", .kestType = std::string{type}});
    return made;
}

std::shared_ptr<const kest::Program> program() {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "game.kest", .text = std::string{kProgram}}};
    auto compiled = kest_library::compile("game.kest", kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

/// A World of `Position`s at `xs`, `obey` run over it once a tick with the
/// doors staging.
struct Run {
    std::shared_ptr<const schema::SchemaRegistry> registry;
    std::unique_ptr<world::World> world;
    std::vector<world::EntityHandle> entities;
    std::unique_ptr<KestSystems> systems;
    std::optional<world::Schedule> schedule;

    Run(CommandDoors& doors, std::vector<float> xs) {
        schema::RegistryBuilder builder;
        builder.add(schema::describeComponent<Position>());
        registry = *builder.freeze();
        world = std::make_unique<world::World>(registry);
        const auto kPosition = *registry->key<Position>();
        for (const float kX : xs) {
            const world::EntityHandle kEntity = *world->create();
            RAWFRAME_EXPECT(world->insert(kEntity, kPosition, Position{kX, 0}).has_value());
            entities.push_back(kEntity);
        }
        kest::DoorTable table;
        RAWFRAME_EXPECT(doors.addDoors(table).has_value());
        const std::array<KestStaging*, 1> kStaging = {&doors};
        const std::array<KestColumn, 2> kColumns = {
            KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write},
            KestColumn{.component = {}, .element = {}, .entities = true}};
        const std::array<KestSystemDeclaration, 1> kDeclarations = {
            KestSystemDeclaration{.identity = "game.obey", .entry = "obey", .columns = kColumns}};
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

    /// Whether tick `tick`'s system succeeded.
    bool at(std::uint64_t tick) {
        world::TickIndex running{tick};
        const auto kReport = schedule->runTick(*world, running, *world::TickRate::of(60));
        return kReport.has_value() && kReport->failures.empty();
    }

    float x(std::size_t index) const {
        return world->get(entities[index], *registry->key<Position>())->x;
    }
};

world_replication::ReceivedCommand push(world::EntityHandle player, float by, std::uint64_t tick) {
    world_replication::ReceivedCommand command{.player = player, .kind = 0, .value = {}, .tick = {tick}};
    command.value.resize(sizeof by);
    std::memcpy(command.value.data(), &by, sizeof by);
    return command;
}

} // namespace

RAWFRAME_TEST(AServersSystemsReadEachCommandOnceFromItsTick) {
    auto doors = CommandDoors::create(game(), *program(), CommandDoors::Role::Read);
    RAWFRAME_EXPECT(doors.has_value());
    if (!doors.has_value()) {
        return;
    }
    RAWFRAME_EXPECT((*doors)->sizes().size() == 1 && (*doors)->sizes()[0] == sizeof(float));
    Run run{**doors, {1, 2, 3}};
    const std::array<world_replication::ReceivedCommand, 3> kDelivered = {
        push(run.entities[0], 10, 2), push(run.entities[2], 5, 2), push(run.entities[2], 1, 4)};
    (*doors)->deliver(kDelivered);
    // Before its tick, no command is read.
    RAWFRAME_EXPECT(run.at(1) && run.x(0) == 1 && run.x(2) == 3);
    // At it, each by its sender's system; the one for a later tick waits.
    RAWFRAME_EXPECT(run.at(2) && run.x(0) == 11 && run.x(1) == 2 && run.x(2) == 8);
    // Once only.
    RAWFRAME_EXPECT(run.at(3) && run.x(0) == 11 && run.x(2) == 8);
    // A tick skipped is no loss: what was due by then is read at the next.
    RAWFRAME_EXPECT(run.at(5) && run.x(2) == 9);
    RAWFRAME_EXPECT(run.at(6) && run.x(2) == 9);
    // Unread while commands for two ticks on arrive, it never will be.
    const std::array<world_replication::ReceivedCommand, 1> kStale = {push(run.entities[1], 100, 7)};
    (*doors)->deliver(kStale);
    const std::array<world_replication::ReceivedCommand, 1> kLater = {push(run.entities[0], 1, 9)};
    (*doors)->deliver(kLater);
    RAWFRAME_EXPECT(run.at(9) && run.x(1) == 2 && run.x(0) == 12);
}

RAWFRAME_TEST(ACommandReadPastItsCountFailsTheRun) {
    auto doors = CommandDoors::create(game(), *program(), CommandDoors::Role::Read);
    RAWFRAME_EXPECT(doors.has_value());
    if (!doors.has_value()) {
        return;
    }
    Run run{**doors, {500}};
    RAWFRAME_EXPECT(!run.at(1));
}

RAWFRAME_TEST(ElsewhereNoCommandIsRead) {
    auto quiet = CommandDoors::create(game(), *program(), CommandDoors::Role::Quiet);
    RAWFRAME_EXPECT(quiet.has_value());
    if (!quiet.has_value()) {
        return;
    }
    Run run{**quiet, {1}};
    const std::array<world_replication::ReceivedCommand, 1> kDelivered = {push(run.entities[0], 10, 1)};
    (*quiet)->deliver(kDelivered);
    RAWFRAME_EXPECT(run.at(1) && run.x(0) == 1);
}

RAWFRAME_TEST(ACommandHoldsNoEntityAndStaysSmall) {
    const auto kCompiled = program();
    // Refused for what they hold, not for being absent.
    RAWFRAME_EXPECT(kCompiled->layout("Pointing").has_value() && kCompiled->layout("Huge").has_value());
    RAWFRAME_EXPECT(!CommandDoors::create(game("Pointing"), *kCompiled, CommandDoors::Role::Read).has_value());
    RAWFRAME_EXPECT(!CommandDoors::create(game("Huge"), *kCompiled, CommandDoors::Role::Read).has_value());
    // The game's own program must lay each out; a sample that never names
    // one sends none of it.
    RAWFRAME_EXPECT(!commandKindsOf(game("Absent"), *kCompiled, true).has_value());
    const auto kLeftOut = commandKindsOf(game("Absent"), *kCompiled, false);
    RAWFRAME_EXPECT(kLeftOut.has_value() && kLeftOut->empty());
}
