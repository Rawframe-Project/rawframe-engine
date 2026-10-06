#include "rawframe/world_kest/layouts.h"

#include "physics_facts.h"
#include "rawframe/base/sha256.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_replication/perception.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

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

bool ofEngineType(const GameComponent& component, std::string_view qualified) {
    const std::size_t kLast = qualified.rfind('.');
    return component.kestType == qualified ||
           (kLast != std::string_view::npos && component.kestType == qualified.substr(kLast + 1));
}

bool laidOutAs(const kest::Program& program,
               std::string_view type,
               std::size_t size,
               std::initializer_list<std::pair<std::string_view, std::size_t>> fields) {
    return laidOutAs(program, type, size, std::span{fields.begin(), fields.size()});
}

bool laidOutAs(const kest::Program& program,
               std::string_view type,
               std::size_t size,
               std::span<const std::pair<std::string_view, std::size_t>> fields) {
    const auto kLayout = program.layout(type);
    if (!kLayout.has_value() || kLayout->size != size || kLayout->fields.size() != fields.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto& [kName, kOffset] : fields) {
        if (kLayout->fields[index].name != kName || kLayout->fields[index].offset != kOffset) {
            return false;
        }
        ++index;
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
    } else if (const auto kAnimation =
                   std::ranges::find(world_animation::componentLayouts(), component.id, &schema::ComponentLayout::id);
               kAnimation != world_animation::componentLayouts().end()) {
        engine = engineLayout(*kAnimation);
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

result::Status checkPhysicsLayouts(const GameDescription& game,
                                   const kest::Program& program,
                                   std::span<const kest::TypeLayout> layouts) {
    if (!game.physics.has_value()) {
        return {};
    }
    const PhysicsFacts kFacts = physicsFacts(game.physics->dimensions);
    const auto kType = [](kest::FieldKind kind) -> std::optional<schema::FieldType> {
        switch (kind) {
        case kest::FieldKind::U8:
            return schema::FieldType::U8;
        case kest::FieldKind::U32:
            return schema::FieldType::U32;
        case kest::FieldKind::U64:
            return schema::FieldType::U64;
        case kest::FieldKind::Bool:
            return schema::FieldType::Bool;
        case kest::FieldKind::F32:
            return schema::FieldType::F32;
        case kest::FieldKind::F64:
            return schema::FieldType::F64;
        default:
            return std::nullopt;
        }
    };
    std::vector<std::pair<const schema::ComponentLayout*, kest::TypeLayout>> checked;
    for (const schema::ComponentLayout& engine : kFacts.components) {
        // parseGame has checked every engine component is one of the game's.
        const auto kComponent = std::ranges::find(game.components, engine.name, &GameComponent::name);
        if (kComponent != game.components.end()) {
            checked.emplace_back(&engine, layouts[static_cast<std::size_t>(kComponent - game.components.begin())]);
        }
    }
    // The queries' answers, those the program uses.
    for (const schema::ComponentLayout& answer : kFacts.answers) {
        if (auto layout = program.layout(answer.scriptType)) {
            checked.emplace_back(&answer, std::move(*layout));
        }
    }
    for (const auto& [kEngine, layout] : checked) {
        const schema::ComponentLayout& engine = *kEngine;
        bool same = layout.size == engine.size && layout.alignment == engine.alignment &&
                    layout.fields.size() == engine.fields.size();
        for (std::size_t index = 0; same && index < layout.fields.size(); ++index) {
            const kest::Field& field = layout.fields[index];
            same = field.name == engine.fields[index].name && field.offset == engine.fields[index].offset &&
                   kType(field.kind) == engine.fields[index].type;
        }
        if (!same) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "the program's physics type is not laid out as the engine's component; import "
                             "the engine's physics module rather than declaring it")
                    .error()
                    .withContext("type", engine.scriptType)
                    .withContext("module", kFacts.module)};
        }
    }
    return {};
}

std::vector<world_runtime::ComponentFieldSet> componentFieldSets(const GameDescription& game,
                                                                 std::span<const kest::TypeLayout> layouts) {
    std::vector<world_runtime::ComponentFieldSet> made;
    for (std::size_t at = 0; at < game.components.size() && at < layouts.size(); ++at) {
        const GameComponent& component = game.components[at];
        const kest::TypeLayout& layout = layouts[at];
        world_runtime::ComponentFieldSet set{.id = component.id, .size = layout.size, .fields = {}};
        std::vector<std::string_view> references;
        for (const GameEntityField& field : game.entityFields) {
            if (field.component == component.name) {
                references.push_back(field.field);
            }
        }
        for (const kest::Field& field : layout.fields) {
            // An entity is its parts in the layout; it is shown whole, at
            // its first part's offset.
            const auto kReference = std::ranges::find_if(references, [&field](std::string_view reference) {
                return field.name.size() > reference.size() && field.name.starts_with(reference) &&
                       field.name[reference.size()] == '.';
            });
            if (kReference != references.end()) {
                const bool kShown =
                    std::ranges::any_of(set.fields, [&](const world_runtime::ComponentFieldEntry& entry) {
                        return entry.name == *kReference;
                    });
                if (!kShown) {
                    set.fields.push_back({.name = std::string{*kReference},
                                          .offset = field.offset,
                                          .kind = world_runtime::ComponentFieldKind::Entity,
                                          .cases = {}});
                }
                continue;
            }
            using K = kest::FieldKind;
            using W = world_runtime::ComponentFieldKind;
            std::optional<W> kind;
            switch (field.kind) {
            case K::I8:
                kind = W::I8;
                break;
            case K::I16:
                kind = W::I16;
                break;
            case K::I32:
                kind = W::I32;
                break;
            case K::I64:
                kind = W::I64;
                break;
            case K::U8:
                kind = W::U8;
                break;
            case K::U16:
                kind = W::U16;
                break;
            case K::U32:
                kind = W::U32;
                break;
            case K::U64:
                kind = W::U64;
                break;
            case K::F32:
                kind = W::F32;
                break;
            case K::F64:
                kind = W::F64;
                break;
            case K::Bool:
                kind = W::Bool;
                break;
            case K::Tag:
                kind = W::Case;
                break;
            case K::Payload:
            case K::Other:
                break;
            }
            if (kind.has_value()) {
                set.fields.push_back({.name = field.name, .offset = field.offset, .kind = *kind, .cases = field.cases});
            }
        }
        made.push_back(std::move(set));
    }
    return made;
}

} // namespace rawframe::world_kest
