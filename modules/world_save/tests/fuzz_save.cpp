// Coverage-guided fuzzing of a save read from disk (D242). A digest is not
// a signature, so the harness gives every input a matching one and the
// reader's other checks do the work: whatever it reads must apply to a
// World that captures it to the same bytes, or be refused whole.

#include "rawframe/base/sha256.h"
#include "rawframe/world_save/save.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

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

const std::shared_ptr<const schema::SchemaRegistry>& registry() {
    static const std::shared_ptr<const schema::SchemaRegistry> kRegistry = [] {
        schema::RegistryBuilder builder;
        builder.add<world::Persistent>().add<Door>().add<Key>();
        return *builder.freeze();
    }();
    return kRegistry;
}

const SaveDeclaration& declaration() {
    static const SaveDeclaration kDeclaration{
        .document = "progress",
        .components = {{.id = Door::kComponentTypeId,
                        .mark = 11,
                        .fields = {{.name = "open", .offset = offsetof(Door, open), .kind = FieldKind::I32},
                                   {.name = "key", .offset = offsetof(Door, key), .kind = FieldKind::Entity}}},
                       {.id = Key::kComponentTypeId,
                        .mark = 12,
                        .fields = {{.name = "teeth", .offset = offsetof(Key, teeth), .kind = FieldKind::I32}}}}};
    return kDeclaration;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::vector<std::byte> saved(size + 32);
    std::copy_n(reinterpret_cast<const std::byte*>(data), size, saved.begin());
    const auto kDigest = base::sha256(std::span{saved}.first(size));
    std::ranges::copy(kDigest, saved.end() - 32);
    const auto kRead = read(saved, declaration(), *registry(), kSpace);
    if (!kRead.has_value() || kRead->migrated) {
        return 0;
    }
    world::World world{registry()};
    if (!apply(*kRead, declaration(), world).has_value()) {
        if (world.entityCount() != 0) {
            std::abort();
        }
        return 0;
    }
    const auto kAgain = capture(world, declaration(), kSpace);
    if (!kAgain.has_value() || *kAgain != saved) {
        std::abort();
    }
    return 0;
}
