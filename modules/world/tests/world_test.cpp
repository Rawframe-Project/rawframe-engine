// Entities and storage: handle validity, slot reuse and retirement, capacity,
// component values through archetype moves, and churn.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"

#include <cstdint>
#include <limits>
#include <map>
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
