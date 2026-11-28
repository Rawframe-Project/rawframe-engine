// Entities and storage: handle validity, slot reuse and retirement, capacity,
// component values through archetype moves, and churn; and the lifecycle of
// what came and went, as readers watch it.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace rawframe::world;
using namespace rawframe::world::testing;

namespace {

bool failedWith(const auto& outcome, WorldError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(DestroyedHandlesGoStaleAndSlotsComeBackNewer) {
    World world{makeRegistry()};
    RAWFRAME_EXPECT(!world.alive(EntityHandle{}));
    const auto kFirst = world.create();
    RAWFRAME_EXPECT(kFirst.has_value() && world.alive(*kFirst));
    RAWFRAME_EXPECT(world.destroy(*kFirst).has_value());
    RAWFRAME_EXPECT(!world.alive(*kFirst));
    RAWFRAME_EXPECT(failedWith(world.destroy(*kFirst), WorldError::StaleEntity));

    const auto kSecond = world.create();
    RAWFRAME_EXPECT(kSecond->slot == kFirst->slot && kSecond->generation == kFirst->generation + 1);
    RAWFRAME_EXPECT(!world.alive(*kFirst) && world.alive(*kSecond));
    RAWFRAME_EXPECT(
        failedWith(world.insert(*kFirst, keysOf(world.registry()).position, Position{}), WorldError::StaleEntity));
    RAWFRAME_EXPECT(world.get(*kFirst, keysOf(world.registry()).position) == nullptr);
    RAWFRAME_EXPECT(world.entityCount() == 1);
}

RAWFRAME_TEST(AWorldCountsTheBytesItHolds) {
    // The World attribution: the tables grow with entities
    // and their values.
    World world{makeRegistry()};
    const std::size_t kEmpty = world.heldBytes();
    for (int count = 0; count < 1000; ++count) {
        const auto kEntity = world.create();
        RAWFRAME_EXPECT(kEntity.has_value() &&
                        world.insert(*kEntity, keysOf(world.registry()).position, Position{}).has_value());
    }
    RAWFRAME_EXPECT(world.heldBytes() >= kEmpty + (1000 * sizeof(Position)));
}

RAWFRAME_TEST(ASlotRetiresAtItsLastGeneration) {
    constexpr std::uint32_t kLast = std::numeric_limits<std::uint32_t>::max();
    World world{makeRegistry(), WorldSettings{.maximumEntities = 4, .firstGeneration = kLast - 1}};
    const auto kOld = world.create();
    RAWFRAME_EXPECT(world.destroy(*kOld).has_value());
    const auto kLastLife = world.create();
    RAWFRAME_EXPECT(kLastLife->slot == kOld->slot && kLastLife->generation == kLast);
    RAWFRAME_EXPECT(world.destroy(*kLastLife).has_value());
    // The slot never comes back: a new one is used instead.
    const auto kFresh = world.create();
    RAWFRAME_EXPECT(kFresh.has_value() && kFresh->slot != kOld->slot);
    RAWFRAME_EXPECT(!world.alive(*kLastLife) && !world.alive(*kOld));
}

RAWFRAME_TEST(CreationStopsAtTheEntityCeiling) {
    World world{makeRegistry(), WorldSettings{.maximumEntities = 3}};
    std::vector<EntityHandle> made;
    for (int index = 0; index < 3; ++index) {
        made.push_back(*world.create());
    }
    RAWFRAME_EXPECT(failedWith(world.create(), WorldError::EntityCapacity));
    RAWFRAME_EXPECT(world.destroy(made[1]).has_value());
    RAWFRAME_EXPECT(world.create().has_value());
}

RAWFRAME_TEST(ComponentsSurviveEveryArchetypeMove) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    std::vector<EntityHandle> entities;
    for (std::int32_t index = 0; index < 20; ++index) {
        const EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insert(kEntity, kKeys.position, Position{index, -index}).has_value());
        entities.push_back(kEntity);
    }
    // Moving every other entity to a new archetype shuffles rows in both.
    for (std::size_t index = 0; index < entities.size(); index += 2) {
        RAWFRAME_EXPECT(world.insert(entities[index], kKeys.velocity, Velocity{1, 2}).has_value());
        RAWFRAME_EXPECT(world.insert(entities[index], kKeys.frozen, Frozen{}).has_value());
    }
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const Position* position = world.get(entities[index], kKeys.position);
        RAWFRAME_EXPECT(position != nullptr && position->x == static_cast<std::int32_t>(index));
        RAWFRAME_EXPECT(world.has(entities[index], kKeys.frozen) == (index % 2 == 0));
        RAWFRAME_EXPECT((world.get(entities[index], kKeys.velocity) != nullptr) == (index % 2 == 0));
    }
    // Replacing keeps the archetype; removing keeps the rest.
    RAWFRAME_EXPECT(world.insert(entities[0], kKeys.position, Position{100, 200}).has_value());
    RAWFRAME_EXPECT(world.get(entities[0], kKeys.position)->y == 200);
    RAWFRAME_EXPECT(world.remove(entities[0], kKeys.velocity).has_value());
    RAWFRAME_EXPECT(world.remove(entities[0], kKeys.velocity).has_value());
    RAWFRAME_EXPECT(world.get(entities[0], kKeys.velocity) == nullptr);
    RAWFRAME_EXPECT(world.get(entities[0], kKeys.position)->x == 100);
    RAWFRAME_EXPECT(world.has(entities[0], kKeys.frozen));
}

RAWFRAME_TEST(EveryValueIsDestroyedExactlyOnce) {
    Tracked::live = 0;
    {
        World world{makeRegistry()};
        const Keys kKeys = keysOf(world.registry());
        std::vector<EntityHandle> entities;
        for (std::int64_t index = 0; index < 50; ++index) {
            const EntityHandle kEntity = *world.create();
            RAWFRAME_EXPECT(world.insert(kEntity, kKeys.tracked, Tracked{index}).has_value());
            entities.push_back(kEntity);
        }
        RAWFRAME_EXPECT(Tracked::live == 50);
        for (std::size_t index = 0; index < entities.size(); index += 3) {
            RAWFRAME_EXPECT(world.insert(entities[index], kKeys.position, Position{}).has_value());
        }
        for (std::size_t index = 1; index < entities.size(); index += 3) {
            RAWFRAME_EXPECT(world.destroy(entities[index]).has_value());
        }
        RAWFRAME_EXPECT(world.remove(entities[2], kKeys.tracked).has_value());
        RAWFRAME_EXPECT(world.insert(entities[3], kKeys.tracked, Tracked{-1}).has_value());
        RAWFRAME_EXPECT(Tracked::live == 50 - 17 - 1);
        RAWFRAME_EXPECT(world.get(entities[3], kKeys.tracked)->value == -1);
        RAWFRAME_EXPECT(world.get(entities[5], kKeys.tracked)->value == 5);
    }
    RAWFRAME_EXPECT(Tracked::live == 0);
}

RAWFRAME_TEST(ALockedStructureRefusesDirectChanges) {
    World world{makeRegistry()};
    const EntityHandle kEntity = *world.create();
    world.lockStructure();
    RAWFRAME_EXPECT(failedWith(world.create(), WorldError::StructureLocked));
    RAWFRAME_EXPECT(failedWith(world.destroy(kEntity), WorldError::StructureLocked));
    RAWFRAME_EXPECT(
        failedWith(world.insert(kEntity, keysOf(world.registry()).position, Position{}), WorldError::StructureLocked));
    world.unlockStructure();
    RAWFRAME_EXPECT(world.destroy(kEntity).has_value());
}

RAWFRAME_TEST(ChurnKeepsHandlesAndValuesConsistent) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    // Seeded, so a failure reproduces.
    std::uint64_t state = 42;
    const auto kNext = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33U);
    };
    std::map<EntityHandle, std::int32_t> expected;
    std::vector<EntityHandle> dead;
    for (int step = 0; step < 50000; ++step) {
        const std::uint32_t kAction = kNext() % 4;
        if (kAction <= 1 || expected.empty()) {
            const EntityHandle kEntity = *world.create();
            const auto kValue = static_cast<std::int32_t>(kNext());
            RAWFRAME_EXPECT(world.insert(kEntity, kKeys.position, Position{kValue, 0}).has_value());
            if (kNext() % 2 == 0) {
                RAWFRAME_EXPECT(world.insert(kEntity, kKeys.velocity, Velocity{}).has_value());
            }
            expected[kEntity] = kValue;
        } else {
            auto victim = expected.begin();
            std::advance(victim, static_cast<std::ptrdiff_t>(kNext() % expected.size()));
            if (kAction == 2) {
                RAWFRAME_EXPECT(world.destroy(victim->first).has_value());
                dead.push_back(victim->first);
                expected.erase(victim);
            } else {
                RAWFRAME_EXPECT(world.remove(victim->first, kKeys.velocity).has_value());
            }
        }
    }
    RAWFRAME_EXPECT(world.entityCount() == expected.size());
    for (const auto& [entity, value] : expected) {
        const Position* position = world.get(entity, kKeys.position);
        RAWFRAME_EXPECT(position != nullptr && position->x == value);
    }
    for (const EntityHandle kEntity : dead) {
        RAWFRAME_EXPECT(!world.alive(kEntity));
    }
}

RAWFRAME_TEST(LayoutOnlyComponentsMoveAsBytes) {
    // A component known only by its layout, as a Kest-declared one is.
    rawframe::schema::ComponentDescriptor pair{
        .id = rawframe::schema::ComponentTypeId::fromText("6f1d2c3b-4a59-4e87-9b6a-0c1d2e3f4a5b"),
        .name = "test.layout_only",
        .size = 8,
        .alignment = 4,
        .plainData = true,
        .operations = {}};
    rawframe::schema::RegistryBuilder builder;
    builder.add(pair).add<Position>();
    auto registry = builder.freeze();
    RAWFRAME_EXPECT(registry.has_value());
    World world{*registry};
    const auto kPair = *(*registry)->find(pair.id);
    const auto kPosition = *(*registry)->key<Position>();
    const EntityHandle kEntity = *world.create();
    std::array<std::int32_t, 2> value = {7, 9};
    RAWFRAME_EXPECT(world.insertErased(kEntity, kPair, value.data()).has_value());
    // Moving the entity to another archetype carries the bytes along.
    RAWFRAME_EXPECT(world.insert(kEntity, kPosition, Position{1, 2}).has_value());
    const auto* stored = static_cast<const std::int32_t*>(world.getErased(kEntity, kPair));
    RAWFRAME_EXPECT(stored != nullptr && stored[0] == 7 && stored[1] == 9);
    value = {3, 4};
    RAWFRAME_EXPECT(world.insertErased(kEntity, kPair, value.data()).has_value());
    stored = static_cast<const std::int32_t*>(world.getErased(kEntity, kPair));
    RAWFRAME_EXPECT(stored[0] == 3 && stored[1] == 4);
    RAWFRAME_EXPECT(world.destroy(kEntity).has_value());
}

RAWFRAME_TEST(TheLifecycleRecordsWhatReadersWatchInTheOrderItHappened) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    Lifecycle& lifecycle = world.lifecycle();
    RAWFRAME_EXPECT(
        lifecycle.watch("cleanup", {.kind = LifecycleKind::Added, .component = kKeys.position.id}).has_value());
    RAWFRAME_EXPECT(
        lifecycle.watch("cleanup", {.kind = LifecycleKind::Removed, .component = kKeys.position.id}).has_value());
    RAWFRAME_EXPECT(lifecycle.watch("cleanup", {.kind = LifecycleKind::Destroyed}).has_value());
    // An addition or removal names a component.
    RAWFRAME_EXPECT(!lifecycle.watch("cleanup", {.kind = LifecycleKind::Added}).has_value());

    const EntityHandle kKept = *world.create();
    RAWFRAME_EXPECT(world.insert(kKept, kKeys.velocity, Velocity{}).has_value());
    CommandBuffer commands;
    const auto kSpawned = commands.create();
    RAWFRAME_EXPECT(commands.insert(*kSpawned, kKeys.position, Position{7, 8}).has_value());
    RAWFRAME_EXPECT(commands.insert(kKept, kKeys.position, Position{1, 2}).has_value());
    // A value replaced is no addition, and velocity is watched by no one.
    RAWFRAME_EXPECT(commands.insert(kKept, kKeys.position, Position{3, 4}).has_value());
    RAWFRAME_EXPECT(commands.remove(kKept, kKeys.velocity).has_value());
    RAWFRAME_EXPECT(commands.remove(kKept, kKeys.position).has_value());
    const auto kReport = world.apply(commands);
    RAWFRAME_EXPECT(kReport.has_value());
    const EntityHandle kCreated = kReport->created[kSpawned->index];
    RAWFRAME_EXPECT(world.destroy(kCreated).has_value());

    const std::span<const LifecycleChange> kSeen = lifecycle.unread("cleanup");
    RAWFRAME_EXPECT(kSeen.size() == 5);
    if (kSeen.size() == 5) {
        RAWFRAME_EXPECT(kSeen[0].kind == LifecycleKind::Added && kSeen[0].entity == kCreated &&
                        kSeen[0].component == kKeys.position.id && kSeen[0].value.empty());
        RAWFRAME_EXPECT(kSeen[1].kind == LifecycleKind::Added && kSeen[1].entity == kKept);
        // A removal keeps the value as it was last, and so does a
        // destruction, before the destruction itself.
        Position last{};
        RAWFRAME_EXPECT(kSeen[2].kind == LifecycleKind::Removed && kSeen[2].entity == kKept &&
                        kSeen[2].value.size() == sizeof last);
        std::memcpy(&last, kSeen[2].value.data(), sizeof last);
        RAWFRAME_EXPECT(last.x == 3 && last.y == 4);
        RAWFRAME_EXPECT(kSeen[3].kind == LifecycleKind::Removed && kSeen[3].entity == kCreated);
        std::memcpy(&last, kSeen[3].value.data(), sizeof last);
        RAWFRAME_EXPECT(last.x == 7 && last.y == 8);
        RAWFRAME_EXPECT(kSeen[4].kind == LifecycleKind::Destroyed && kSeen[4].entity == kCreated &&
                        !kSeen[4].component.has_value());
        for (std::size_t index = 0; index < kSeen.size(); ++index) {
            RAWFRAME_EXPECT(kSeen[index].sequence == index);
        }
    }

    // A reader made now reads from now; read, the first has nothing left,
    // and what neither still wants is let go.
    RAWFRAME_EXPECT(lifecycle.watch("late", {.kind = LifecycleKind::Destroyed}).has_value());
    RAWFRAME_EXPECT(lifecycle.unread("late").empty() && lifecycle.reader("late")->cursor == 5);
    RAWFRAME_EXPECT(world.destroy(kKept).has_value());
    RAWFRAME_EXPECT(lifecycle.unread("late").size() == 1 && lifecycle.unread("cleanup").size() == 6);
    lifecycle.markRead("cleanup");
    RAWFRAME_EXPECT(lifecycle.unread("cleanup").empty() && lifecycle.changes().size() == 1);
    lifecycle.markRead("late");
    RAWFRAME_EXPECT(lifecycle.changes().empty() && lifecycle.nextSequence() == 6 && lifecycle.dropped() == 0);
    RAWFRAME_EXPECT(lifecycle.unread("nobody").empty());
}

RAWFRAME_TEST(TheLifecycleIsBoundedAndRestoredOnlyWhole) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    Lifecycle& lifecycle = world.lifecycle();
    RAWFRAME_EXPECT(lifecycle.watch("slow", {.kind = LifecycleKind::Destroyed}).has_value());
    // A reader that never reads keeps changes to the bound; past it the
    // oldest go, counted, and it reads from the oldest kept.
    for (std::size_t made = 0; made < kMaximumLifecycleChanges + 10; ++made) {
        RAWFRAME_EXPECT(world.destroy(*world.create()).has_value());
    }
    RAWFRAME_EXPECT(lifecycle.changes().size() == kMaximumLifecycleChanges && lifecycle.dropped() == 10);
    RAWFRAME_EXPECT(lifecycle.unread("slow").size() == kMaximumLifecycleChanges &&
                    lifecycle.unread("slow").front().sequence == 10);
    // Readers are bounded, and so are the kinds one watches.
    for (std::size_t reader = 1; reader < kMaximumLifecycleReaders; ++reader) {
        RAWFRAME_EXPECT(
            lifecycle.watch("reader." + std::to_string(reader), {.kind = LifecycleKind::Destroyed}).has_value());
    }
    RAWFRAME_EXPECT(!lifecycle.watch("one.too.many", {.kind = LifecycleKind::Destroyed}).has_value());
    RAWFRAME_EXPECT(!lifecycle.watch("", {.kind = LifecycleKind::Destroyed}).has_value());

    // A restore takes consecutive changes below the next sequence, and
    // replaces everything.
    Lifecycle restored;
    const LifecycleReader kReader{.identity = "cleanup",
                                  .cursor = 4,
                                  .watches = {{.kind = LifecycleKind::Removed, .component = kKeys.position.id}}};
    const auto kChange = [&](std::uint64_t sequence) {
        return LifecycleChange{.sequence = sequence,
                               .kind = LifecycleKind::Removed,
                               .component = kKeys.position.id,
                               .entity = {},
                               .value = {std::byte{1}}};
    };
    RAWFRAME_EXPECT(!restored.restore({kReader}, {kChange(5), kChange(4)}, 7, 0).has_value());
    RAWFRAME_EXPECT(!restored.restore({kReader}, {kChange(4)}, 4, 0).has_value());
    RAWFRAME_EXPECT(!restored.restore({kReader, kReader}, {}, 7, 0).has_value());
    RAWFRAME_EXPECT(
        !restored.restore({LifecycleReader{.identity = "x", .cursor = 9, .watches = {}}}, {}, 7, 0).has_value());
    // Changes may skip, as a checkpoint leaves out what it does not carry.
    RAWFRAME_EXPECT(restored.restore({kReader}, {kChange(3), kChange(4), kChange(6)}, 7, 3).has_value());
    RAWFRAME_EXPECT(restored.unread("cleanup").size() == 2 && restored.unread("cleanup").front().sequence == 4 &&
                    restored.dropped() == 3 && restored.nextSequence() == 7 &&
                    restored.watched({.kind = LifecycleKind::Removed, .component = kKeys.position.id}));
}
