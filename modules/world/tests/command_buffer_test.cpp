// Command buffers: recording order, pending entities, stale targets, value
// lifetimes, capacity, and the all-or-nothing commit.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/errors.h"

#include <array>
#include <span>

using namespace rawframe::world;
using namespace rawframe::world::testing;

RAWFRAME_TEST(CommandsApplyInRecordingOrder) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    const EntityHandle kExisting = *world.create();
    RAWFRAME_EXPECT(world.insert(kExisting, kKeys.velocity, Velocity{}).has_value());

    CommandBuffer commands;
    const auto kSpawned = commands.create();
    RAWFRAME_EXPECT(kSpawned.has_value());
    RAWFRAME_EXPECT(commands.insert(*kSpawned, kKeys.position, Position{7, 8}).has_value());
    RAWFRAME_EXPECT(commands.insert(*kSpawned, kKeys.frozen, Frozen{}).has_value());
    RAWFRAME_EXPECT(commands.insert(kExisting, kKeys.position, Position{1, 1}).has_value());
    RAWFRAME_EXPECT(commands.insert(kExisting, kKeys.position, Position{2, 2}).has_value());
    RAWFRAME_EXPECT(commands.remove(kExisting, kKeys.velocity).has_value());
    RAWFRAME_EXPECT(commands.size() == 6);
    // Nothing changes until the barrier.
    RAWFRAME_EXPECT(world.entityCount() == 1 && world.get(kExisting, kKeys.position) == nullptr);

    const auto kReport = world.apply(commands);
    RAWFRAME_EXPECT(kReport.has_value() && kReport->applied == 6 && kReport->skippedStale == 0);
    RAWFRAME_EXPECT(commands.empty());
    const EntityHandle kCreated = kReport->created[kSpawned->index];
    RAWFRAME_EXPECT(world.get(kCreated, kKeys.position)->y == 8);
    RAWFRAME_EXPECT(world.has(kCreated, kKeys.frozen));
    // The later insert wins.
    RAWFRAME_EXPECT(world.get(kExisting, kKeys.position)->x == 2);
    RAWFRAME_EXPECT(!world.has(kExisting, kKeys.velocity));
}

RAWFRAME_TEST(CommandsOnDeadEntitiesAreSkippedAndCounted) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    const EntityHandle kDoomed = *world.create();
    CommandBuffer first;
    CommandBuffer second;
    // Two systems both decide the entity dies, and one also edits it.
    RAWFRAME_EXPECT(first.destroy(kDoomed).has_value());
    RAWFRAME_EXPECT(second.destroy(kDoomed).has_value());
    RAWFRAME_EXPECT(second.insert(kDoomed, kKeys.position, Position{}).has_value());
    const auto kFirst = world.apply(first);
    const auto kSecond = world.apply(second);
    RAWFRAME_EXPECT(kFirst->applied == 1 && kSecond->applied == 0 && kSecond->skippedStale == 2);
    RAWFRAME_EXPECT(world.entityCount() == 0);
}

RAWFRAME_TEST(BufferedValuesLiveExactlyUntilApplyOrClear) {
    Tracked::live = 0;
    {
        World world{makeRegistry()};
        const Keys kKeys = keysOf(world.registry());
        const EntityHandle kEntity = *world.create();
        CommandBuffer commands;
        RAWFRAME_EXPECT(commands.insert(kEntity, kKeys.tracked, Tracked{5}).has_value());
        RAWFRAME_EXPECT(commands.insert(kEntity, kKeys.tracked, Tracked{6}).has_value());
        RAWFRAME_EXPECT(Tracked::live == 2);
        RAWFRAME_EXPECT(world.apply(commands).has_value());
        RAWFRAME_EXPECT(Tracked::live == 1 && world.get(kEntity, kKeys.tracked)->value == 6);

        RAWFRAME_EXPECT(commands.insert(kEntity, kKeys.tracked, Tracked{7}).has_value());
        commands.clear();
        RAWFRAME_EXPECT(Tracked::live == 1);
        RAWFRAME_EXPECT(commands.insert(kEntity, kKeys.tracked, Tracked{8}).has_value());
    }
    // The buffer's destructor dropped the last unapplied value.
    RAWFRAME_EXPECT(Tracked::live == 0);
}

RAWFRAME_TEST(RecordingStopsAtEitherLimit) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    const EntityHandle kEntity = *world.create();
    CommandBuffer few{CommandBufferSettings{.maximumCommands = 2, .maximumValueBytes = 1024}};
    RAWFRAME_EXPECT(few.destroy(kEntity).has_value());
    RAWFRAME_EXPECT(few.destroy(kEntity).has_value());
    const auto kThird = few.destroy(kEntity);
    RAWFRAME_EXPECT(!kThird.has_value() && kThird.error().code() == code(WorldError::CommandCapacity));

    CommandBuffer small{CommandBufferSettings{.maximumCommands = 100, .maximumValueBytes = sizeof(Position) * 2}};
    RAWFRAME_EXPECT(small.insert(kEntity, kKeys.position, Position{}).has_value());
    RAWFRAME_EXPECT(small.insert(kEntity, kKeys.position, Position{}).has_value());
    RAWFRAME_EXPECT(!small.insert(kEntity, kKeys.position, Position{}).has_value());
    // A tag needs no value space.
    RAWFRAME_EXPECT(small.insert(kEntity, kKeys.frozen, Frozen{}).has_value());
}

RAWFRAME_TEST(ACommitThatCannotFitAppliesNothing) {
    World world{makeRegistry(), WorldSettings{.maximumEntities = 2}};
    const Keys kKeys = keysOf(world.registry());
    const EntityHandle kEntity = *world.create();
    CommandBuffer commands;
    RAWFRAME_EXPECT(commands.insert(kEntity, kKeys.position, Position{3, 3}).has_value());
    RAWFRAME_EXPECT(commands.create().has_value());
    RAWFRAME_EXPECT(commands.create().has_value());
    const auto kReport = world.apply(commands);
    RAWFRAME_EXPECT(!kReport.has_value() && kReport.error().code() == code(WorldError::EntityCapacity));
    RAWFRAME_EXPECT(world.entityCount() == 1 && world.get(kEntity, kKeys.position) == nullptr);
    RAWFRAME_EXPECT(commands.empty());
}

RAWFRAME_TEST(ApplyIsTheBarrierAndNeedsTheStructureUnlocked) {
    World world{makeRegistry()};
    CommandBuffer commands;
    RAWFRAME_EXPECT(commands.create().has_value());
    world.lockStructure();
    const auto kLocked = world.apply(commands);
    RAWFRAME_EXPECT(!kLocked.has_value() && kLocked.error().code() == code(WorldError::StructureLocked));
    world.unlockStructure();
    RAWFRAME_EXPECT(world.apply(commands).has_value() && world.entityCount() == 1);
}

RAWFRAME_TEST(PlainDataIsBufferedFromBytes) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    const auto& kPosition = world.registry().descriptor(kKeys.position.id);
    CommandBuffer commands;
    const auto kSpawned = commands.create();
    const Position kValue{5, 6};
    RAWFRAME_EXPECT(commands.insertBytes(*kSpawned, kKeys.position.id, kPosition, std::as_bytes(std::span{&kValue, 1}))
                        .has_value());
    const auto kReport = world.apply(commands);
    RAWFRAME_EXPECT(kReport.has_value() && kReport->created.size() == 1);
    const EntityHandle kEntity = kReport->created[0];
    RAWFRAME_EXPECT(world.get(kEntity, kKeys.position)->x == 5 && world.get(kEntity, kKeys.position)->y == 6);

    commands.clear();
    RAWFRAME_EXPECT(commands.removeErased(kEntity, kKeys.position.id).has_value());
    RAWFRAME_EXPECT(world.apply(commands).has_value());
    RAWFRAME_EXPECT(world.get(kEntity, kKeys.position) == nullptr);

    // Bytes of another size, or a component that is not plain data, are refused.
    commands.clear();
    const std::int32_t kShort = 1;
    const auto kWrongSize =
        commands.insertBytes(kEntity, kKeys.position.id, kPosition, std::as_bytes(std::span{&kShort, 1}));
    RAWFRAME_EXPECT(!kWrongSize.has_value() && kWrongSize.error().code() == code(WorldError::InvalidValue));
    const auto& kTracked = world.registry().descriptor(kKeys.tracked.id);
    std::array<std::byte, sizeof(Tracked)> bytes{};
    RAWFRAME_EXPECT(!commands.insertBytes(kEntity, kKeys.tracked.id, kTracked, bytes).has_value());
    RAWFRAME_EXPECT(commands.empty());
}
