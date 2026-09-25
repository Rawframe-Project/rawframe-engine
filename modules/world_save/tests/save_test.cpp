// Curated saves: a level's door and the key it names captured, then applied
// to a new World of the same level, updating what it holds and making what
// it lacks; the same capture giving the same bytes; and every way a save or
// a World can refuse.

#include "rawframe/base/sha256.h"
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
        .components = {{.id = Door::kComponentTypeId, .mark = 11, .entityFields = {offsetof(Door, key)}},
                       {.id = Key::kComponentTypeId, .mark = 12, .entityFields = {}}}};
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
    later[8] = std::byte{2};
    RAWFRAME_EXPECT(refusedWith(kRead(redigested(later)), SaveError::TooNew));
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, base::Bits128{.high = 9, .low = 9}), SaveError::Mismatch));
    std::vector<std::byte> longer = kSaved;
    longer.insert(longer.end() - 32, std::byte{0});
    RAWFRAME_EXPECT(refusedWith(kRead(redigested(longer)), SaveError::Malformed));
    // A declaration whose door is laid out otherwise.
    SaveDeclaration changed = declaration();
    changed.components[0].mark = 99;
    RAWFRAME_EXPECT(refusedWith(read(kSaved, changed, *kRegistry, kSpace), SaveError::Mismatch));
    // Over the limits.
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, kSpace, {.maximumEntities = 1}), SaveError::LimitExceeded));
    RAWFRAME_EXPECT(
        refusedWith(read(kSaved, declaration(), *kRegistry, kSpace, {.maximumBytes = 64}), SaveError::LimitExceeded));
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
