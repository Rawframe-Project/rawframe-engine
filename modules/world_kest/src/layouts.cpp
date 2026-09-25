#include "rawframe/world_kest/layouts.h"

#include "physics_facts.h"
#include "rawframe/base/sha256.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_replication/perception.h"

#include <array>
#include <cstddef>

namespace rawframe::world_kest {

namespace {

kest::TypeLayout perceptionLayout() {
    using world_replication::Perception;
    kest::TypeLayout made{
        .size = sizeof(Perception),
        .alignment = alignof(Perception),
        .mark = 0,
        .fields = {
            kest::Field{.name = "baseTick", .offset = offsetof(Perception, baseTick), .kind = kest::FieldKind::U64},
            kest::Field{.name = "fraction", .offset = offsetof(Perception, fraction), .kind = kest::FieldKind::U16},
            kest::Field{.name = "viewer", .offset = offsetof(Perception, viewer), .kind = kest::FieldKind::U32}}};
    made.mark = engineLayoutMark(made);
    return made;
}

kest::TypeLayout persistentLayout() {
    using world::Persistent;
    kest::TypeLayout made{
        .size = sizeof(Persistent),
        .alignment = alignof(Persistent),
        .mark = 0,
        .fields = {kest::Field{.name = "high", .offset = offsetof(Persistent, high), .kind = kest::FieldKind::U64},
                   kest::Field{.name = "low", .offset = offsetof(Persistent, low), .kind = kest::FieldKind::U64}}};
    made.mark = engineLayoutMark(made);
    return made;
}

/// The engine's layout of one of its components.
kest::TypeLayout engineLayout(const schema::ComponentLayout& engine) {
    constexpr std::array<kest::FieldKind, 6> kKinds = {kest::FieldKind::U8,
                                                       kest::FieldKind::U32,
                                                       kest::FieldKind::U64,
                                                       kest::FieldKind::Bool,
                                                       kest::FieldKind::F32,
                                                       kest::FieldKind::F64};
    kest::TypeLayout made{.size = engine.size, .alignment = engine.alignment, .mark = 0, .fields = {}};
    for (const schema::ComponentField& field : engine.fields) {
        made.fields.push_back(kest::Field{.name = std::string{field.name},
                                          .offset = field.offset,
                                          .kind = kKinds[static_cast<std::size_t>(field.type)]});
    }
    made.mark = engineLayoutMark(made);
    return made;
}

} // namespace

std::uint64_t engineLayoutMark(const kest::TypeLayout& layout) {
    base::Sha256 digest;
    const auto kNumber = [&digest](std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            bytes[at] = static_cast<std::byte>(value >> (8U * (7U - at)));
        }
        digest.update(bytes);
    };
    digest.update("rawframe.world_kest.engine_layout.v1");
    kNumber(layout.size);
    kNumber(layout.alignment);
    kNumber(layout.fields.size());
    for (const kest::Field& field : layout.fields) {
        kNumber(field.name.size());
        digest.update(field.name);
        kNumber(field.offset);
        kNumber(static_cast<std::uint64_t>(field.kind));
    }
    const base::Sha256Digest kMade = digest.finish();
    std::uint64_t mark = 0;
    for (std::size_t at = 0; at < 8; ++at) {
        mark = (mark << 8U) | std::to_integer<std::uint64_t>(kMade[at]);
    }
    return mark;
}

bool sameLayout(const kest::TypeLayout& left, const kest::TypeLayout& right) {
    if (left.size != right.size || left.alignment != right.alignment || left.fields.size() != right.fields.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.fields.size(); ++index) {
        if (left.fields[index].name != right.fields[index].name ||
            left.fields[index].offset != right.fields[index].offset ||
            left.fields[index].kind != right.fields[index].kind) {
            return false;
        }
    }
    return true;
}

result::Result<kest::TypeLayout>
componentLayout(const GameDescription& game, const kest::Program& program, const GameComponent& component) {
    auto layout = program.layout(component.kestType);
    std::optional<kest::TypeLayout> engine;
    if (component.id == world_replication::Perception::kComponentTypeId) {
        engine = perceptionLayout();
    } else if (component.id == world::Persistent::kComponentTypeId) {
        engine = persistentLayout();
    } else if (component.id == world_animation::Animator::kComponentTypeId) {
        engine = engineLayout(world_animation::componentLayouts().front());
    } else if (game.physics.has_value()) {
        for (const schema::ComponentLayout& owned : physicsFacts(game.physics->dimensions).components) {
            if (owned.id == component.id) {
                engine = engineLayout(owned);
            }
        }
    }
    if (!engine.has_value()) {
        return layout;
    }
    // Engine-made either way: the program's, if it names the type, must be
    // the engine's.
    if (layout.has_value() && !sameLayout(*layout, *engine)) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument,
                         kWorldKestDomain,
                         code(WorldKestError::BadGameLine),
                         "the program lays out an engine component otherwise than the engine; import its module")
                .error()
                .withContext("name", component.name)};
    }
    return *engine;
}

} // namespace rawframe::world_kest
