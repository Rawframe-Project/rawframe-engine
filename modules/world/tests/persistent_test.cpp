// Persistent identity: scene-derived identities pinned by golden values,
// drawn ones following the World's seed, and duplicates found.

#include "rawframe/schema/registry.h"
#include "rawframe/test/test.h"
#include "rawframe/world/persistent.h"

#include <memory>

using namespace rawframe;
using namespace rawframe::world;

namespace {

std::shared_ptr<const schema::SchemaRegistry> persistentRegistry() {
    schema::RegistryBuilder builder;
    builder.add<Persistent>();
    auto registry = builder.freeze();
    return registry.has_value() ? *registry : nullptr;
}

} // namespace

RAWFRAME_TEST(ASceneEntitysIdentityFollowsTheSceneAndTheEntity) {
    const base::Bits128 kLevel{.high = 0x52771075251e7361, .low = 0xdeaecf4939c72e56};
    const base::Bits128 kDoor{.high = 0x1000, .low = 7};
    const PersistentEntityId kId = persistentFromSource(kLevel, kDoor);
    // A versioned derivation: this value is its identity.
    RAWFRAME_EXPECT(kId.value.high == 0xfd314d1f3f977a5d && kId.value.low == 0x34ecf39451e88ea5);
    RAWFRAME_EXPECT(persistentFromSource(kLevel, kDoor) == kId);
    RAWFRAME_EXPECT(persistentFromSource(kLevel, base::Bits128{.high = 0x1000, .low = 8}) != kId);
    RAWFRAME_EXPECT(persistentFromSource(base::Bits128{.high = 1, .low = 1}, kDoor) != kId);
}

RAWFRAME_TEST(ADrawnIdentityFollowsTheWorldsSeed) {
    const auto kRegistry = persistentRegistry();
    World first{kRegistry, WorldSettings{.rootSeed = RootSeed{42}}};
    World again{kRegistry, WorldSettings{.rootSeed = RootSeed{42}}};
    World other{kRegistry, WorldSettings{.rootSeed = RootSeed{43}}};
    const PersistentEntityId kFirst = newPersistentId(first);
    RAWFRAME_EXPECT(kFirst == newPersistentId(again) && kFirst != newPersistentId(other));
    RAWFRAME_EXPECT(newPersistentId(first) != kFirst);
    RAWFRAME_EXPECT(kFirst.value != base::Bits128{});
    // The draw is World state a checkpoint carries.
    RAWFRAME_EXPECT(
        first.randomStreams().contains({std::string{kPersistentStreamOwner}, std::string{kPersistentStreamName}}));
}

RAWFRAME_TEST(DuplicateIdentitiesAreFound) {
    const auto kRegistry = persistentRegistry();
    World world{kRegistry};
    const auto kKey = *kRegistry->key<Persistent>();
    RAWFRAME_EXPECT(persistentDuplicates(world).empty());
    const Persistent kShared{.high = 5, .low = 6};
    for (const Persistent kValue :
         {kShared, Persistent{.high = 5, .low = 7}, kShared, kShared, Persistent{}, Persistent{}}) {
        const auto kEntity = world.create();
        RAWFRAME_EXPECT(kEntity.has_value() && world.insert(*kEntity, kKey, kValue).has_value());
    }
    const auto kTwice = persistentDuplicates(world);
    // The shared one, once; two unnamed entities are not a duplicate.
    RAWFRAME_EXPECT(kTwice.size() == 1 && kTwice[0] == kShared.id());
    // A World whose registry lacks the component has none.
    schema::RegistryBuilder empty;
    World without{*empty.freeze()};
    RAWFRAME_EXPECT(persistentDuplicates(without).empty());
}

RAWFRAME_TEST(InsertingNoughtNamesTheEntity) {
    const auto kRegistry = persistentRegistry();
    World world{kRegistry, WorldSettings{.rootSeed = RootSeed{7}}};
    World twin{kRegistry, WorldSettings{.rootSeed = RootSeed{7}}};
    const auto kKey = *kRegistry->key<Persistent>();
    const auto kEntity = *world.create();
    RAWFRAME_EXPECT(world.insert(kEntity, kKey, Persistent{}).has_value());
    const Persistent kNamed = *world.get(kEntity, kKey);
    // Named from the stream, as a twin World draws it.
    RAWFRAME_EXPECT(kNamed.named() && kNamed.id() == newPersistentId(twin));
    // Nought again keeps the name; a named value replaces it, as any value.
    RAWFRAME_EXPECT(world.insert(kEntity, kKey, Persistent{}).has_value() &&
                    world.get(kEntity, kKey)->id() == kNamed.id());
    RAWFRAME_EXPECT(world.insert(kEntity, kKey, Persistent{.high = 1, .low = 2}).has_value() &&
                    world.get(kEntity, kKey)->high == 1);
}
