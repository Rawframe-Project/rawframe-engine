// Curated saves: a level's door and the key it names captured, then applied
// to a new World of the same level, updating what it holds and making what
// it lacks; the same capture giving the same bytes; every way a save or a
// World can refuse; and hostile saves with a matching digest applied only as
// they capture.

#include "rawframe/base/sha256.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_save/errors.h"
#include "rawframe/world_save/save.h"

#include <cstring>
#include <memory>

using namespace rawframe;
using namespace rawframe::world_save;

namespace {

struct Door {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("0b8e2d71-4c5a-4f93-8e16-3a7d9c2b5f40");
    static constexpr std::string_view kComponentName = "test.door";

    std::int32_t open = 0;
    std::uint32_t padding = 0;
    world::EntityHandle key;
};

struct Key {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("5d3a8f06-1e7b-4c29-b4d0-8f2e6a1c9b73");
    static constexpr std::string_view kComponentName = "test.key";

    std::int32_t teeth = 0;
};

const base::Bits128 kSpace{.high = 0x5a5e, .low = 1};
const world::Persistent kDoorName{.high = 1, .low = 1};
const world::Persistent kKeyName{.high = 1, .low = 2};

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<world::Persistent>().add<Door>().add<Key>();
    return *builder.freeze();
}

SaveDeclaration declaration() {
    return SaveDeclaration{
        .document = "progress",
        .components = {{.id = Door::kComponentTypeId,
                        .mark = 11,
                        .fields = {{.name = "open", .offset = offsetof(Door, open), .kind = FieldKind::I32},
                                   {.name = "key", .offset = offsetof(Door, key), .kind = FieldKind::Entity}}},
                       {.id = Key::kComponentTypeId,
                        .mark = 12,
                        .fields = {{.name = "teeth", .offset = offsetof(Key, teeth), .kind = FieldKind::I32}}}}};
}

struct Keys {
    schema::ComponentKey<world::Persistent> persistent;
    schema::ComponentKey<Door> door;
    schema::ComponentKey<Key> key;
};

Keys keysOf(const schema::SchemaRegistry& registry) {
    return Keys{
        .persistent = *registry.key<world::Persistent>(), .door = *registry.key<Door>(), .key = *registry.key<Key>()};
}

/// The World a level makes: a persistent door, closed and keyless.
world::EntityHandle level(world::World& world) {
    const Keys kKeys = keysOf(world.registry());
    const world::EntityHandle kDoor = *world.create();
    RAWFRAME_EXPECT(world.insert(kDoor, kKeys.persistent, kDoorName).has_value());
    RAWFRAME_EXPECT(world.insert(kDoor, kKeys.door, Door{}).has_value());
    return kDoor;
}

std::vector<std::byte> redigested(std::vector<std::byte> bytes) {
    const auto kDigest = base::sha256(std::span{bytes}.first(bytes.size() - 32));
    std::ranges::copy(kDigest, bytes.end() - 32);
    return bytes;
}

bool refusedWith(const auto& outcome, SaveError error) {
    return !outcome.has_value() && outcome.error().domain() == kSaveDomain && outcome.error().code() == code(error);
}

/// The played World: the door opened with a key the player made, and
/// things the save does not keep.
std::vector<std::byte> played() {
    const auto kRegistry = registry();
    const Keys kKeys = keysOf(*kRegistry);
    world::World world{kRegistry};
    const world::EntityHandle kDoor = level(world);
    const world::EntityHandle kKey = *world.create();
    RAWFRAME_EXPECT(world.insert(kKey, kKeys.persistent, kKeyName).has_value());
    RAWFRAME_EXPECT(world.insert(kKey, kKeys.key, Key{.teeth = 5}).has_value());
    RAWFRAME_EXPECT(world.insert(kDoor, kKeys.door, Door{.open = 1, .key = kKey}).has_value());
    // A door that is not persistent, and a persistent entity with nothing
    // the document keeps.
    const world::EntityHandle kLoose = *world.create();
    RAWFRAME_EXPECT(world.insert(kLoose, kKeys.door, Door{.open = 3}).has_value());
    const world::EntityHandle kBare = *world.create();
    RAWFRAME_EXPECT(world.insert(kBare, kKeys.persistent, world::Persistent{.high = 1, .low = 3}).has_value());
    auto bytes = capture(world, declaration(), kSpace);
    RAWFRAME_EXPECT(bytes.has_value());
    // The same World captures to the same bytes.
    RAWFRAME_EXPECT(bytes.has_value() && capture(world, declaration(), kSpace) == *bytes);
    return bytes.has_value() ? *bytes : std::vector<std::byte>{};
}

} // namespace

RAWFRAME_TEST(ASaveAppliesToANewWorldOfTheSameLevel) {
    const std::vector<std::byte> kSaved = played();
    const auto kRegistry = registry();
    const Keys kKeys = keysOf(*kRegistry);
    world::World world{kRegistry};
    const world::EntityHandle kDoor = level(world);
    const auto kStaged = read(kSaved, declaration(), *kRegistry, kSpace);
    RAWFRAME_EXPECT(kStaged.has_value() && kStaged->entities.size() == 2);
    if (!kStaged.has_value()) {
        return;
    }
    const auto kApplied = apply(*kStaged, declaration(), world);
    RAWFRAME_EXPECT(kApplied.has_value() && kApplied->updated == 1 && kApplied->created == 1);
    // The level's door opened, naming a key made for the save's key, which
    // carries the key's identity.
    const Door* door = world.get(kDoor, kKeys.door);
    RAWFRAME_EXPECT(door != nullptr && door->open == 1 && world.alive(door->key));
    if (door != nullptr && world.alive(door->key)) {
        RAWFRAME_EXPECT(world.get(door->key, kKeys.key)->teeth == 5);
        RAWFRAME_EXPECT(world.get(door->key, kKeys.persistent)->id() == kKeyName.id());
    }
    RAWFRAME_EXPECT(world.entityCount() == 2);
}

RAWFRAME_TEST(ADeclaredComponentTheSaveLacksIsRemoved) {
    const std::vector<std::byte> kSaved = played();
    const auto kRegistry = registry();
    const Keys kKeys = keysOf(*kRegistry);
    world::World world{kRegistry};
    level(world);
    // The key's entity exists here, and is also a door, which the save
    // says it is not.
    const world::EntityHandle kKey = *world.create();
    RAWFRAME_EXPECT(world.insert(kKey, kKeys.persistent, kKeyName).has_value());
    RAWFRAME_EXPECT(world.insert(kKey, kKeys.door, Door{.open = 9}).has_value());
    const auto kApplied = apply(*read(kSaved, declaration(), *kRegistry, kSpace), declaration(), world);
    RAWFRAME_EXPECT(kApplied.has_value() && kApplied->updated == 2 && kApplied->created == 0);
    RAWFRAME_EXPECT(!world.has(kKey, kKeys.door) && world.get(kKey, kKeys.key)->teeth == 5);
}

RAWFRAME_TEST(ASaveIsHostileInput) {
    const std::vector<std::byte> kSaved = played();
    const auto kRegistry = registry();
    const auto kRead = [&kRegistry](std::span<const std::byte> bytes) {
        return read(bytes, declaration(), *kRegistry, kSpace);
    };
    RAWFRAME_EXPECT(kRead(kSaved).has_value());
    // A flipped byte anywhere, and a cut anywhere.
    for (std::size_t at = 0; at < kSaved.size(); at += 7) {
        std::vector<std::byte> flipped = kSaved;
        flipped[at] ^= std::byte{0x40};
        RAWFRAME_EXPECT(!kRead(flipped).has_value());
        RAWFRAME_EXPECT(!kRead(std::span{kSaved}.first(at)).has_value());
    }
    std::vector<std::byte> tampered = kSaved;
    tampered[20] ^= std::byte{1};
    RAWFRAME_EXPECT(refusedWith(kRead(tampered), SaveError::DigestMismatch));
    // With the digest made to match: a later format, another namespace, and
    // a byte past the end.
    std::vector<std::byte> later = kSaved;
    later[8] = std::byte{3};
    RAWFRAME_EXPECT(refusedWith(kRead(redigested(later)), SaveError::TooNew));
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, base::Bits128{.high = 9, .low = 9}), SaveError::Mismatch));
    std::vector<std::byte> longer = kSaved;
    longer.insert(longer.end() - 32, std::byte{0});
    RAWFRAME_EXPECT(refusedWith(kRead(redigested(longer)), SaveError::Malformed));
    // A declaration whose door is laid out otherwise, with a field it cannot
    // be migrated to, or with no fields to migrate by.
    SaveDeclaration narrowed = declaration();
    narrowed.components[0].mark = 99;
    narrowed.components[0].fields[0].kind = FieldKind::I16;
    RAWFRAME_EXPECT(refusedWith(read(kSaved, narrowed, *kRegistry, kSpace), SaveError::Mismatch));
    SaveDeclaration bare = declaration();
    bare.components[0].mark = 99;
    bare.components[0].fields.clear();
    RAWFRAME_EXPECT(refusedWith(read(kSaved, bare, *kRegistry, kSpace), SaveError::Mismatch));
    // Another document.
    SaveDeclaration other = declaration();
    other.document = "elsewhere";
    RAWFRAME_EXPECT(refusedWith(read(kSaved, other, *kRegistry, kSpace), SaveError::Mismatch));
    // Over the limits.
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, kSpace, {.maximumEntities = 1}), SaveError::LimitExceeded));
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, kSpace, {.maximumBytes = 64}), SaveError::LimitExceeded));
}

RAWFRAME_TEST(HostileSavesWithAMatchingDigestApplyOnlyAsTheyCapture) {
    const std::vector<std::byte> kSaved = played();
    const auto kRegistry = registry();
    const std::string_view kSeed{reinterpret_cast<const char*>(kSaved.data()), kSaved.size() - 32};
    test::Mutations mutations;
    std::size_t accepted = 0;
    for (int round = 0; round < 4000; ++round) {
        const std::string kText = mutations.mutate(kSeed, std::string_view{"\x00\x01\x02\x08\x0b\x0c\xff", 7});
        std::vector<std::byte> damaged(kText.size() + 32);
        std::memcpy(damaged.data(), kText.data(), kText.size());
        damaged = redigested(std::move(damaged));
        const auto kRead = read(damaged, declaration(), *kRegistry, kSpace);
        if (!kRead.has_value() || kRead->migrated) {
            continue;
        }
        // What is read applies to a World that captures it to the same
        // bytes, or is refused whole.
        world::World world{kRegistry};
        if (!apply(*kRead, declaration(), world).has_value()) {
            RAWFRAME_EXPECT(world.entityCount() == 0);
            continue;
        }
        ++accepted;
        const auto kAgain = capture(world, declaration(), kSpace);
        RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == damaged);
    }
    RAWFRAME_EXPECT(accepted > 0);
}

RAWFRAME_TEST(WhatCannotBeSavedOrAppliedIsRefused) {
    const auto kRegistry = registry();
    const Keys kKeys = keysOf(*kRegistry);
    // A door naming a key with no identity.
    world::World world{kRegistry};
    const world::EntityHandle kDoor = level(world);
    const world::EntityHandle kLoose = *world.create();
    RAWFRAME_EXPECT(world.insert(kDoor, kKeys.door, Door{.open = 1, .key = kLoose}).has_value());
    RAWFRAME_EXPECT(refusedWith(capture(world, declaration(), kSpace), SaveError::UnnamedReference));
    // Two entities holding one identity.
    world::World doubled{kRegistry};
    level(doubled);
    level(doubled);
    RAWFRAME_EXPECT(refusedWith(capture(doubled, declaration(), kSpace), SaveError::DuplicateIdentity));
    // A reference to an identity neither the save nor the World holds
    // leaves the World as it was.
    StagedSave staged;
    staged.entities.push_back(
        StagedSave::Entity{.id = kDoorName.id(),
                           .values = {std::vector<std::byte>(sizeof(Door)), std::nullopt},
                           .references = {{world::PersistentEntityId{base::Bits128{.high = 7, .low = 7}}}, {}}});
    world::World fresh{kRegistry};
    const world::EntityHandle kFreshDoor = level(fresh);
    RAWFRAME_EXPECT(refusedWith(apply(staged, declaration(), fresh), SaveError::UnknownReference));
    RAWFRAME_EXPECT(fresh.entityCount() == 1 && fresh.get(kFreshDoor, kKeys.door)->open == 0);
    // A declaration of what the World does not have, or of one component
    // twice.
    SaveDeclaration unknown = declaration();
    unknown.components[1].id = schema::ComponentTypeId::fromText("11111111-1111-4111-8111-111111111111");
    RAWFRAME_EXPECT(refusedWith(capture(world, unknown, kSpace), SaveError::InvalidDeclaration));
    SaveDeclaration twice = declaration();
    twice.components[1] = twice.components[0];
    RAWFRAME_EXPECT(refusedWith(capture(world, twice, kSpace), SaveError::InvalidDeclaration));
}

RAWFRAME_TEST(OneEntityIsSavedAndAppliedUnderAnIdentityItNeedNotCarry) {
    const auto kRegistry = registry();
    const Keys kKeys = keysOf(*kRegistry);
    const world::PersistentEntityId kPlayer{base::Bits128{.high = 0x9, .low = 0x9}};
    // A player, not persistent, holding a door that names a persistent key.
    world::World played{kRegistry};
    const world::EntityHandle kKey = *played.create();
    RAWFRAME_EXPECT(played.insert(kKey, kKeys.persistent, kKeyName).has_value());
    const world::EntityHandle kHero = *played.create();
    RAWFRAME_EXPECT(played.insert(kHero, kKeys.door, Door{.open = 4, .key = kKey}).has_value());
    const auto kBytes = captureEntity(played, declaration(), kSpace, kHero, kPlayer);
    RAWFRAME_EXPECT(kBytes.has_value());
    if (!kBytes.has_value()) {
        return;
    }
    const auto kStaged = read(*kBytes, declaration(), *kRegistry, kSpace);
    RAWFRAME_EXPECT(kStaged.has_value() && kStaged->entities.size() == 1 && kStaged->entities[0].id == kPlayer);

    // The player joins another World holding the key: the door is theirs
    // again, naming this World's key, and nothing else is made.
    world::World joined{kRegistry};
    const world::EntityHandle kHereKey = *joined.create();
    RAWFRAME_EXPECT(joined.insert(kHereKey, kKeys.persistent, kKeyName).has_value());
    const world::EntityHandle kNewcomer = *joined.create();
    const auto kApplied = applyTo(*kStaged, declaration(), joined, kNewcomer, kPlayer);
    RAWFRAME_EXPECT(kApplied.has_value() && kApplied->updated == 1 && kApplied->created == 0);
    const Door* door = joined.get(kNewcomer, kKeys.door);
    RAWFRAME_EXPECT(door != nullptr && door->open == 4 && door->key == kHereKey);
    RAWFRAME_EXPECT(!joined.has(kNewcomer, kKeys.persistent) && joined.entityCount() == 2);
    // Under another identity, it is not this player's.
    RAWFRAME_EXPECT(
        refusedWith(applyTo(*kStaged, declaration(), joined, kNewcomer, kDoorName.id()), SaveError::Mismatch));
}

namespace {

/// The door a later version of the game lays out: the key first, the open
/// count widened, and a creak it did not have. Same component identity.
struct DoorLater {
    static constexpr schema::ComponentTypeId kComponentTypeId = Door::kComponentTypeId;
    static constexpr std::string_view kComponentName = "test.door";

    world::EntityHandle key;
    std::int64_t open = 0;
    std::int32_t creak = 0;
    std::uint32_t padding = 0;
};

} // namespace

RAWFRAME_TEST(ASaveOfAnEarlierDeclarationIsMigratedByFieldName) {
    const std::vector<std::byte> kSaved = played();
    schema::RegistryBuilder builder;
    builder.add<world::Persistent>().add<DoorLater>();
    const auto kLater = *builder.freeze();
    // The door as it is now, and the key no longer kept.
    const SaveDeclaration kNow{
        .document = "progress",
        .components = {{.id = Door::kComponentTypeId,
                        .mark = 21,
                        .fields = {{.name = "key", .offset = offsetof(DoorLater, key), .kind = FieldKind::Entity},
                                   {.name = "open", .offset = offsetof(DoorLater, open), .kind = FieldKind::I64},
                                   {.name = "creak", .offset = offsetof(DoorLater, creak), .kind = FieldKind::I32}}}}};
    const auto kStaged = read(kSaved, kNow, *kLater, kSpace);
    RAWFRAME_EXPECT(kStaged.has_value());
    if (!kStaged.has_value()) {
        return;
    }
    // The key's entity kept only a key, which is dropped, so only the door
    // is left; its open count widened, its creak new, its key still named.
    RAWFRAME_EXPECT(kStaged->migrated && kStaged->entities.size() == 1 && kStaged->entities[0].id == kDoorName.id());
    DoorLater door;
    std::memcpy(static_cast<void*>(&door), kStaged->entities[0].values[0]->data(), sizeof door);
    RAWFRAME_EXPECT(door.open == 1 && door.creak == 0 && door.key.isNull());
    RAWFRAME_EXPECT(kStaged->entities[0].references[0].size() == 1 &&
                    kStaged->entities[0].references[0][0] == kKeyName.id());
    // Read under the declaration it was written with, nothing migrates.
    const auto kRegistry = registry();
    RAWFRAME_EXPECT(!read(kSaved, declaration(), *kRegistry, kSpace)->migrated);
}
