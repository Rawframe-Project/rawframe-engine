// A game's guaranteed messages at its doors (D266): a server's systems send
// them, kept only when the run that sent them succeeds, in order; a client's
// present systems read what arrived; a message holding an entity is refused.

#include "../src/message_doors.h"
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
        schema::ComponentTypeId::fromText("0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5b2");
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

struct Moved {
    x: f32
}

extern fn Messages.moved(to: world.Entity, value: Moved)
extern fn ReceivedCount.moved() -> i32
extern fn Received.moved(index: i32) -> Moved

// Tells each its place, then refuses past a hundred: what it sent then is
// not kept.
fn tell(count: i32, positions: [Position], entities: [world.Entity]) {
    let i = 0
    while i < count {
        Messages.moved(entities[i], Moved(positions[i].x))
        i = i + 1
    }
    if positions[0].x > 100.0 {
        positions[count].x = 0.0
    }
}

// Takes the last message that arrived.
fn read(count: i32, positions: [Position]) {
    let arrived = ReceivedCount.moved()
    let i = 0
    while i < count {
        if arrived > 0 {
            positions[i].y = Received.moved(arrived - 1).x
        }
        i = i + 1
    }
}
)";

GameDescription game(std::string_view type = "Moved") {
    GameDescription made;
    made.messages.push_back(GameMessage{.name = "moved", .kestType = std::string{type}});
    return made;
}

std::shared_ptr<const kest::Program> program() {
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "game.kest", .text = std::string{kProgram}}};
    auto compiled = kest_library::compile("game.kest", kFiles, {});
    RAWFRAME_EXPECT(compiled.has_value());
    return compiled.has_value() ? *compiled : nullptr;
}

/// A World of `Position`s at `xs`, `system` run over it once a tick with
/// `doors` staging.
struct Run {
    std::shared_ptr<const schema::SchemaRegistry> registry;
    std::unique_ptr<world::World> world;
    std::vector<world::EntityHandle> entities;
    std::unique_ptr<KestSystems> systems;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;

    Run(MessageDoors& doors, std::string_view entry, std::span<const KestColumn> columns, std::vector<float> xs) {
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
        const std::array<KestSystemDeclaration, 1> kDeclarations = {
            KestSystemDeclaration{.identity = "game.system", .entry = entry, .columns = columns}};
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
        const auto kReport = schedule->runTick(*world, tick, *world::TickRate::of(60));
        return kReport.has_value() && kReport->failures.empty();
    }

    Position at(std::size_t index) const {
        return *world->get(entities[index], *registry->key<Position>());
    }
};

float valueOf(const world_replication::PostedMessage& message) {
    float value = 0;
    RAWFRAME_EXPECT(message.value.size() == sizeof value);
    std::memcpy(&value, message.value.data(), sizeof value);
    return value;
}

} // namespace

RAWFRAME_TEST(AServersMessagesAreKeptWithTheRunThatSentThem) {
    const GameDescription kGame = game();
    auto doors = MessageDoors::create(kGame, *program(), MessageDoors::Role::Send);
    RAWFRAME_EXPECT(doors.has_value() && (*doors)->largest() == sizeof(float));
    if (!doors.has_value()) {
        return;
    }
    const std::array<KestColumn, 2> kColumns = {
        KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write},
        KestColumn{.component = {}, .element = {}, .entities = true}};
    Run run{**doors, "tell", kColumns, {1, 2, 3}};
    RAWFRAME_EXPECT(run.once());
    std::vector<world_replication::PostedMessage> taken;
    (*doors)->take(taken);
    RAWFRAME_EXPECT(taken.size() == 3);
    if (taken.size() == 3) {
        for (std::size_t index = 0; index < 3; ++index) {
            RAWFRAME_EXPECT(taken[index].player == run.entities[index] && taken[index].kind == 0 &&
                            valueOf(taken[index]) == static_cast<float>(index + 1));
        }
    }
    // Taken once.
    taken.clear();
    (*doors)->take(taken);
    RAWFRAME_EXPECT(taken.empty());
    // A run that sends and then refuses sends nothing.
    Run refused{**doors, "tell", kColumns, {500, 2}};
    RAWFRAME_EXPECT(!refused.once());
    (*doors)->take(taken);
    RAWFRAME_EXPECT(taken.empty());
    // Elsewhere, sending does nothing.
    auto quiet = MessageDoors::create(kGame, *program(), MessageDoors::Role::Quiet);
    RAWFRAME_EXPECT(quiet.has_value());
    if (quiet.has_value()) {
        Run elsewhere{**quiet, "tell", kColumns, {1}};
        RAWFRAME_EXPECT(elsewhere.once());
        (*quiet)->take(taken);
        RAWFRAME_EXPECT(taken.empty());
    }
}

RAWFRAME_TEST(AClientReadsTheMessagesThatArrived) {
    auto doors = MessageDoors::create(game(), *program(), MessageDoors::Role::Read);
    RAWFRAME_EXPECT(doors.has_value());
    if (!doors.has_value()) {
        return;
    }
    const std::array<KestColumn, 1> kColumns = {
        KestColumn{.component = Position::kComponentTypeId, .element = "Position", .access = world::Access::Write}};
    Run run{**doors, "read", kColumns, {0}};
    const auto kMessage = [](std::uint32_t kind, float x, std::size_t size = sizeof(float)) {
        world_replication::ReceivedMessage made{.kind = kind, .value = std::vector<std::byte>(size)};
        std::memcpy(made.value.data(), &x, std::min(size, sizeof x));
        return made;
    };
    // The last of what arrived for the tick; one of another kind, or not
    // its kind's size, is left unread.
    const std::array<world_replication::ReceivedMessage, 4> kArrived = {
        kMessage(0, 4), kMessage(0, 7), kMessage(3, 9), kMessage(0, 11, 8)};
    (*doors)->arrived(kArrived);
    RAWFRAME_EXPECT(run.once() && run.at(0).y == 7);
    // A tick with nothing arrived reads nothing.
    (*doors)->arrived({});
    RAWFRAME_EXPECT(run.once() && run.at(0).y == 7);
}

RAWFRAME_TEST(AMessageHoldsNoEntity) {
    RAWFRAME_EXPECT(
        !MessageDoors::create(game("rawframe.world.Entity"), *program(), MessageDoors::Role::Send).has_value());
    RAWFRAME_EXPECT(!MessageDoors::create(game("Unknown"), *program(), MessageDoors::Role::Send).has_value());
}
