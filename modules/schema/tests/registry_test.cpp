// The component registry: stable IDs, deterministic runtime indices, typed
// keys, and every refusal.

#include "rawframe/schema/errors.h"
#include "rawframe/schema/registry.h"
#include "rawframe/test/test.h"

using namespace rawframe::schema;

namespace {

struct Position {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("9b4c1a52-3d4e-4f60-8a71-0c2d3e4f5a6b");
    static constexpr std::string_view kComponentName = "test.position";
    float x = 0;
    float y = 0;
};

struct Frozen {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d");
    static constexpr std::string_view kComponentName = "test.frozen";
};

struct Impostor {
    static constexpr ComponentTypeId kComponentTypeId = Position::kComponentTypeId;
    static constexpr std::string_view kComponentName = "test.impostor";
    int value = 0;
};

struct Renamed {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("ffffffff-0000-4000-8000-000000000001");
    static constexpr std::string_view kComponentName = "test.position";
};

// Everything a component needs except being plain data.
struct Virtual {
    [[maybe_unused]] static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("11111111-2222-4333-8444-555555555555");
    [[maybe_unused]] static constexpr std::string_view kComponentName = "test.virtual";
    virtual ~Virtual() = default;
};

static_assert(Component<Position> && Component<Frozen>);
static_assert(!Component<Virtual>, "a type with virtual functions is not a component");
static_assert(!Component<int>, "a type without a stable identity is not a component");
static_assert(describeComponent<Frozen>().size == 0, "an empty type is a tag with no storage");

bool failedWith(const auto& outcome, SchemaError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(StableIdTextIsCanonicalUuidOnly) {
    RAWFRAME_EXPECT(parseStableIdText("9b4c1a52-3d4e-4f60-8a71-0c2d3e4f5a6b").parsed);
    RAWFRAME_EXPECT(!parseStableIdText("9B4C1A52-3D4E-4F60-8A71-0C2D3E4F5A6B").parsed);
    RAWFRAME_EXPECT(!parseStableIdText("9b4c1a523d4e4f608a710c2d3e4f5a6b").parsed);
    RAWFRAME_EXPECT(!parseStableIdText("9b4c1a52-3d4e-4f60-8a71_0c2d3e4f5a6b").parsed);
    RAWFRAME_EXPECT(!parseStableIdText("{9b4c1a52-3d4e-4f60-8a71-0c2d3e4f5a6}").parsed);
    RAWFRAME_EXPECT(Position::kComponentTypeId.value.high == 0x9b4c1a523d4e4f60ULL);
}

RAWFRAME_TEST(RuntimeIndicesFollowStableIdOrder) {
    RegistryBuilder forward;
    forward.add<Position>().add<Frozen>();
    RegistryBuilder backward;
    backward.add<Frozen>().add<Position>();
    const auto kForward = forward.freeze();
    const auto kBackward = backward.freeze();
    RAWFRAME_EXPECT(kForward.has_value() && kBackward.has_value());
    if (!kForward.has_value() || !kBackward.has_value()) {
        return;
    }
    const SchemaRegistry& registry = **kForward;
    const auto kFrozenKey = registry.key<Frozen>();
    const auto kPositionKey = registry.key<Position>();
    RAWFRAME_EXPECT(kFrozenKey.has_value() && kFrozenKey->id.value == 0);
    RAWFRAME_EXPECT(kPositionKey.has_value() && kPositionKey->id.value == 1);
    RAWFRAME_EXPECT((*kBackward)->key<Position>()->id == kPositionKey->id);
    RAWFRAME_EXPECT(registry.descriptor(kPositionKey->id).size == sizeof(Position));
    RAWFRAME_EXPECT(registry.owns(*kPositionKey));
    RAWFRAME_EXPECT(!registry.owns(ComponentKey<Position>{ComponentRuntimeId{0}}));
    RAWFRAME_EXPECT(!registry.owns(ComponentKey<Position>{ComponentRuntimeId{7}}));
}

RAWFRAME_TEST(UnknownTypesHaveNoKey) {
    RegistryBuilder builder;
    builder.add<Frozen>();
    const auto kRegistry = builder.freeze();
    RAWFRAME_EXPECT(kRegistry.has_value() && failedWith((*kRegistry)->key<Position>(), SchemaError::UnknownComponent));
}

RAWFRAME_TEST(DuplicatesAndInvalidDescriptorsAreRefused) {
    RegistryBuilder sameId;
    sameId.add<Position>().add<Impostor>();
    RAWFRAME_EXPECT(failedWith(sameId.freeze(), SchemaError::DuplicateComponentId));

    RegistryBuilder sameName;
    sameName.add<Position>().add<Renamed>();
    RAWFRAME_EXPECT(failedWith(sameName.freeze(), SchemaError::DuplicateComponentName));

    ComponentDescriptor zero = describeComponent<Position>();
    zero.id = ComponentTypeId{};
    RegistryBuilder zeroId;
    zeroId.add(zero);
    RAWFRAME_EXPECT(failedWith(zeroId.freeze(), SchemaError::InvalidComponentId));

    ComponentDescriptor unnamed = describeComponent<Position>();
    unnamed.name = {};
    RegistryBuilder noName;
    noName.add(unnamed);
    RAWFRAME_EXPECT(failedWith(noName.freeze(), SchemaError::InvalidComponentName));

    ComponentDescriptor bare = describeComponent<Position>();
    bare.operations = {};
    bare.plainData = false;
    RegistryBuilder noOperations;
    noOperations.add(bare);
    RAWFRAME_EXPECT(failedWith(noOperations.freeze(), SchemaError::MissingOperations));
    // Plain data may go without: it moves as bytes.
    bare.plainData = true;
    RegistryBuilder plain;
    plain.add(bare);
    RAWFRAME_EXPECT(plain.freeze().has_value());

    RAWFRAME_EXPECT(RegistryBuilder{}.freeze().has_value());
}
