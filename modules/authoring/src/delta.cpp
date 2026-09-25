#include "rawframe/authoring/delta.h"

#include "delta_parts.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <tuple>

namespace rawframe::authoring {

std::unexpected<result::Error> deltaInvalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::DeltaInvalid), why);
}

std::unexpected<result::Error> deltaMismatch(std::string_view why) {
    return result::fail(
        result::ErrorClass::FailedPrecondition, kAuthoringDomain, code(AuthoringError::DeltaMismatch), why);
}

bool componentKind(DeltaKind kind) {
    return kind == DeltaKind::AddComponent || kind == DeltaKind::RemoveComponent || kind == DeltaKind::SetField ||
           kind == DeltaKind::SetReference || kind == DeltaKind::SetMark;
}

bool fieldKind(DeltaKind kind) {
    return kind == DeltaKind::SetField || kind == DeltaKind::SetReference;
}

namespace {

/// Only the member the kind uses, or none.
bool holdsOnly(const SlotValue& slot, DeltaKind kind) {
    const bool kNode = kind == DeltaKind::CreateNode || kind == DeltaKind::DestroyNode;
    const bool kComponent = kind == DeltaKind::AddComponent || kind == DeltaKind::RemoveComponent;
    return (kNode || !slot.node.has_value()) && (kind == DeltaKind::Reorder || !slot.place.has_value()) &&
           (kind == DeltaKind::SetName || !slot.name.has_value()) && (kComponent || !slot.component.has_value()) &&
           (fieldKind(kind) || !slot.field.has_value()) && (kind == DeltaKind::SetMark || !slot.mark.has_value()) &&
           (kind == DeltaKind::SetOverride || !slot.patch.has_value()) &&
           (kind == DeltaKind::CreateInstance || kind == DeltaKind::DestroyInstance || !slot.instance.has_value());
}

/// An instance record named by the delta's entity, its marks exactly the
/// components its patch names.
bool instanceShaped(const InstanceRecord& instance, base::Bits128 entity) {
    std::vector<std::string_view> named;
    for (const scene::Override& each : instance.overrides) {
        if (!each.component.empty()) {
            named.push_back(each.component);
        }
    }
    std::ranges::sort(named);
    const auto kRepeated = std::ranges::unique(named);
    named.erase(kRepeated.begin(), kRepeated.end());
    return !instance.entities.empty() && instance.entities.front().instance == entity &&
           std::ranges::equal(named, instance.marks, {}, {}, &scene::SchemaMark::component);
}

/// A patch entry in its kind's form for the slot: the entity's removal has
/// no mark and no fields; a component's entry has its mark.
bool patchShaped(const std::optional<PatchRecord>& patch, bool whole) {
    if (!patch.has_value()) {
        return true;
    }
    if (whole) {
        return patch->kind == scene::Override::Kind::Remove && !patch->mark.has_value() && patch->fields.empty();
    }
    return patch->mark.has_value() && (patch->kind != scene::Override::Kind::Remove || patch->fields.empty());
}

bool fieldOfKind(const std::optional<scene::FieldValue>& value, bool reference) {
    if (!value.has_value()) {
        return true;
    }
    return reference ? value->kind == scene::FieldValue::Kind::Entity
                     : value->kind == scene::FieldValue::Kind::Number || value->kind == scene::FieldValue::Kind::True;
}

} // namespace

bool shaped(const Delta& delta) {
    if ((delta.entity == base::Bits128{}) != (delta.kind == DeltaKind::SetMark) ||
        !holdsOnly(delta.before, delta.kind) || !holdsOnly(delta.after, delta.kind) ||
        (delta.kind != DeltaKind::SetOverride && componentKind(delta.kind) == delta.component.empty()) ||
        fieldKind(delta.kind) == delta.field.empty()) {
        return false;
    }
    const SlotValue& before = delta.before;
    const SlotValue& after = delta.after;
    switch (delta.kind) {
    case DeltaKind::CreateNode:
        return !before.node.has_value() && after.node.has_value();
    case DeltaKind::DestroyNode:
        return before.node.has_value() && !after.node.has_value();
    case DeltaKind::Reorder:
        return before.place.has_value() && after.place.has_value();
    case DeltaKind::SetName:
        return before.name.has_value() && after.name.has_value();
    case DeltaKind::AddComponent:
        return !before.component.has_value() && after.component.has_value() && after.component->name == delta.component;
    case DeltaKind::RemoveComponent:
        return before.component.has_value() && !after.component.has_value() &&
               before.component->name == delta.component;
    case DeltaKind::SetField:
    case DeltaKind::SetReference: {
        const bool kReference = delta.kind == DeltaKind::SetReference;
        return fieldOfKind(before.field, kReference) && fieldOfKind(after.field, kReference);
    }
    case DeltaKind::SetMark:
        return before.mark.has_value() && after.mark.has_value();
    case DeltaKind::SetOverride:
        return (before.patch.has_value() || after.patch.has_value()) &&
               patchShaped(before.patch, delta.component.empty()) && patchShaped(after.patch, delta.component.empty());
    case DeltaKind::CreateInstance:
        return !before.instance.has_value() && after.instance.has_value() &&
               instanceShaped(*after.instance, delta.entity);
    case DeltaKind::DestroyInstance:
        return before.instance.has_value() && !after.instance.has_value() &&
               instanceShaped(*before.instance, delta.entity);
    }
    return false;
}

namespace {

/// Where the instance named by `entity`, its first mapping's id, stands.
std::optional<std::size_t> namedInstance(const scene::Scene& scene, base::Bits128 entity) {
    const auto kFound = std::ranges::find_if(scene.instances, [entity](const scene::SceneInstance& instance) {
        return !instance.entities.empty() && instance.entities.front().instance == entity;
    });
    return kFound != scene.instances.end() ? std::optional{static_cast<std::size_t>(kFound - scene.instances.begin())}
                                           : std::nullopt;
}

/// Where the instance that maps `entity` stands, if one does.
std::optional<std::size_t> instanceOf(const scene::Scene& scene, base::Bits128 entity) {
    const auto kFound = std::ranges::find_if(scene.instances, [entity](const scene::SceneInstance& instance) {
        return std::ranges::contains(instance.entities, entity, &scene::IdentityMapping::instance);
    });
    return kFound != scene.instances.end() ? std::optional{static_cast<std::size_t>(kFound - scene.instances.begin())}
                                           : std::nullopt;
}

std::optional<std::size_t> placeOf(const scene::Scene& scene, base::Bits128 entity) {
    const auto kFound = std::ranges::find(scene.entities, entity, &scene::SceneEntity::id);
    return kFound != scene.entities.end() ? std::optional{static_cast<std::size_t>(kFound - scene.entities.begin())}
                                          : std::nullopt;
}

std::optional<std::uint64_t> markOf(const scene::Scene& scene, std::string_view component) {
    const auto kFound = std::ranges::find(scene.schema, component, &scene::SchemaMark::component);
    return kFound != scene.schema.end() ? std::optional{kFound->mark} : std::nullopt;
}

ComponentRecord recordOf(const scene::Scene& scene, const scene::SceneComponent& component) {
    return ComponentRecord{
        .name = component.name, .mark = markOf(scene, component.name).value_or(0), .fields = component.fields};
}

/// Whether the schema may hold `mark` for `component`, and holds it after.
bool useMark(scene::Scene& scene, const std::string& component, std::uint64_t mark) {
    const std::optional<std::uint64_t> kHeld = markOf(scene, component);
    if (kHeld.has_value()) {
        return *kHeld == mark;
    }
    const auto kAt = std::ranges::lower_bound(scene.schema, component, {}, &scene::SchemaMark::component);
    scene.schema.insert(kAt, scene::SchemaMark{.component = component, .mark = mark});
    return true;
}

/// Drops the marks of components nothing uses any more.
void dropUnusedMarks(scene::Scene& scene) {
    std::set<std::string, std::less<>> used;
    for (const scene::SceneEntity& entity : scene.entities) {
        for (const scene::SceneComponent& component : entity.components) {
            used.insert(component.name);
        }
    }
    for (const scene::SceneInstance& instance : scene.instances) {
        for (const scene::Override& each : instance.overrides) {
            if (!each.component.empty()) {
                used.insert(each.component);
            }
        }
    }
    std::erase_if(scene.schema, [&used](const scene::SchemaMark& mark) {
        return !used.contains(mark.component);
    });
}

/// The slot's value now, as the delta's kind reads it; none when the slot
/// is not there to read (a field of an entity that is not).
std::optional<SlotValue> slotOf(const scene::Scene& scene, const Delta& delta) {
    SlotValue slot;
    if (delta.kind == DeltaKind::SetMark) {
        const auto kFound = std::ranges::find(scene.schema, delta.component, &scene::SchemaMark::component);
        if (kFound == scene.schema.end()) {
            return std::nullopt;
        }
        slot.mark = kFound->mark;
        return slot;
    }
    if (delta.kind == DeltaKind::CreateInstance || delta.kind == DeltaKind::DestroyInstance) {
        const std::optional<std::size_t> kNamed = namedInstance(scene, delta.entity);
        if (kNamed.has_value()) {
            const scene::SceneInstance& instance = scene.instances[*kNamed];
            InstanceRecord record{.place = *kNamed,
                                  .scene = instance.scene,
                                  .entities = instance.entities,
                                  .overrides = instance.overrides,
                                  .marks = {}};
            for (const scene::Override& each : instance.overrides) {
                if (!each.component.empty() &&
                    !std::ranges::contains(record.marks, each.component, &scene::SchemaMark::component)) {
                    record.marks.push_back(scene::SchemaMark{.component = each.component,
                                                             .mark = markOf(scene, each.component).value_or(0)});
                }
            }
            std::ranges::sort(record.marks, {}, &scene::SchemaMark::component);
            slot.instance = std::move(record);
        }
        return slot;
    }
    if (delta.kind == DeltaKind::SetOverride) {
        const std::optional<std::size_t> kInstance = instanceOf(scene, delta.entity);
        if (!kInstance.has_value()) {
            return std::nullopt;
        }
        const std::vector<scene::Override>& overrides = scene.instances[*kInstance].overrides;
        const auto kFound = std::ranges::find_if(overrides, [&delta](const scene::Override& each) {
            return each.entity == delta.entity && each.component == delta.component;
        });
        if (kFound != overrides.end()) {
            slot.patch =
                PatchRecord{.kind = kFound->kind,
                            .mark = delta.component.empty() ? std::nullopt
                                                            : std::optional{markOf(scene, delta.component).value_or(0)},
                            .fields = kFound->fields};
        }
        return slot;
    }
    const std::optional<std::size_t> kPlace = placeOf(scene, delta.entity);
    if (delta.kind == DeltaKind::CreateNode || delta.kind == DeltaKind::DestroyNode) {
        if (kPlace.has_value()) {
            const scene::SceneEntity& entity = scene.entities[*kPlace];
            NodeRecord node{.place = *kPlace, .name = entity.name, .components = {}};
            for (const scene::SceneComponent& component : entity.components) {
                node.components.push_back(recordOf(scene, component));
            }
            slot.node = std::move(node);
        }
        return slot;
    }
    if (!kPlace.has_value()) {
        return std::nullopt;
    }
    const scene::SceneEntity& entity = scene.entities[*kPlace];
    if (delta.kind == DeltaKind::Reorder) {
        slot.place = *kPlace;
        return slot;
    }
    if (delta.kind == DeltaKind::SetName) {
        slot.name = entity.name;
        return slot;
    }
    const auto kComponent = std::ranges::find(entity.components, delta.component, &scene::SceneComponent::name);
    if (!fieldKind(delta.kind)) {
        if (kComponent != entity.components.end()) {
            slot.component = recordOf(scene, *kComponent);
        }
        return slot;
    }
    if (kComponent == entity.components.end()) {
        return std::nullopt;
    }
    const auto kField = std::ranges::find(kComponent->fields, delta.field, &scene::SceneField::name);
    if (kField != kComponent->fields.end()) {
        slot.field = kField->value;
    }
    return slot;
}

/// Writes `to` into the slot, whose present value the caller has checked.
result::Status writeSlot(scene::Scene& scene, const Delta& delta, const SlotValue& to) {
    const std::optional<std::size_t> kPlace = placeOf(scene, delta.entity);
    switch (delta.kind) {
    case DeltaKind::CreateNode:
    case DeltaKind::DestroyNode: {
        if (kPlace.has_value()) {
            scene.entities.erase(scene.entities.begin() + static_cast<std::ptrdiff_t>(*kPlace));
        }
        if (to.node.has_value()) {
            if (to.node->place > scene.entities.size()) {
                return deltaMismatch("an entity is made at a place the scene has");
            }
            scene::SceneEntity made{.id = delta.entity, .name = to.node->name, .components = {}};
            for (const ComponentRecord& component : to.node->components) {
                if (!useMark(scene, component.name, component.mark)) {
                    return deltaMismatch("a component's mark is the one the scene's schema holds");
                }
                made.components.push_back(scene::SceneComponent{.name = component.name, .fields = component.fields});
            }
            scene.entities.insert(scene.entities.begin() + static_cast<std::ptrdiff_t>(to.node->place),
                                  std::move(made));
        }
        dropUnusedMarks(scene);
        return {};
    }
    case DeltaKind::Reorder: {
        if (*to.place >= scene.entities.size()) {
            return deltaMismatch("an entity moves to a place the scene has");
        }
        scene::SceneEntity moved = std::move(scene.entities[*kPlace]);
        scene.entities.erase(scene.entities.begin() + static_cast<std::ptrdiff_t>(*kPlace));
        scene.entities.insert(scene.entities.begin() + static_cast<std::ptrdiff_t>(*to.place), std::move(moved));
        return {};
    }
    case DeltaKind::SetName:
        scene.entities[*kPlace].name = *to.name;
        return {};
    case DeltaKind::AddComponent:
    case DeltaKind::RemoveComponent: {
        std::vector<scene::SceneComponent>& components = scene.entities[*kPlace].components;
        std::erase_if(components, [&delta](const scene::SceneComponent& each) {
            return each.name == delta.component;
        });
        if (to.component.has_value()) {
            if (!useMark(scene, to.component->name, to.component->mark)) {
                return deltaMismatch("a component's mark is the one the scene's schema holds");
            }
            const auto kAt = std::ranges::lower_bound(components, delta.component, {}, &scene::SceneComponent::name);
            components.insert(kAt, scene::SceneComponent{.name = delta.component, .fields = to.component->fields});
        }
        dropUnusedMarks(scene);
        return {};
    }
    case DeltaKind::SetField:
    case DeltaKind::SetReference: {
        std::vector<scene::SceneComponent>& components = scene.entities[*kPlace].components;
        std::vector<scene::SceneField>& fields =
            std::ranges::find(components, delta.component, &scene::SceneComponent::name)->fields;
        std::erase_if(fields, [&delta](const scene::SceneField& each) {
            return each.name == delta.field;
        });
        if (to.field.has_value()) {
            const auto kAt = std::ranges::lower_bound(fields, delta.field, {}, &scene::SceneField::name);
            fields.insert(kAt, scene::SceneField{.name = delta.field, .value = *to.field});
        }
        return {};
    }
    case DeltaKind::SetMark:
        std::ranges::find(scene.schema, delta.component, &scene::SchemaMark::component)->mark = *to.mark;
        return {};
    case DeltaKind::CreateInstance:
    case DeltaKind::DestroyInstance: {
        const std::optional<std::size_t> kNamed = namedInstance(scene, delta.entity);
        if (kNamed.has_value()) {
            scene.instances.erase(scene.instances.begin() + static_cast<std::ptrdiff_t>(*kNamed));
        }
        if (to.instance.has_value()) {
            if (to.instance->place > scene.instances.size()) {
                return deltaMismatch("an instance is made at a place the scene has");
            }
            for (const scene::SchemaMark& mark : to.instance->marks) {
                if (!useMark(scene, mark.component, mark.mark)) {
                    return deltaMismatch("a component's mark is the one the scene's schema holds");
                }
            }
            scene.instances.insert(scene.instances.begin() + static_cast<std::ptrdiff_t>(to.instance->place),
                                   scene::SceneInstance{.scene = to.instance->scene,
                                                        .entities = to.instance->entities,
                                                        .overrides = to.instance->overrides});
        }
        dropUnusedMarks(scene);
        return {};
    }
    case DeltaKind::SetOverride: {
        std::vector<scene::Override>& overrides = scene.instances[*instanceOf(scene, delta.entity)].overrides;
        std::erase_if(overrides, [&delta](const scene::Override& each) {
            return each.entity == delta.entity && each.component == delta.component;
        });
        if (to.patch.has_value()) {
            if (to.patch->mark.has_value() && !useMark(scene, delta.component, *to.patch->mark)) {
                return deltaMismatch("a component's mark is the one the scene's schema holds");
            }
            const auto kAt = std::ranges::find_if(overrides, [&delta](const scene::Override& each) {
                return std::tie(delta.entity, delta.component) < std::tie(each.entity, each.component);
            });
            overrides.insert(kAt,
                             scene::Override{.entity = delta.entity,
                                             .component = delta.component,
                                             .kind = to.patch->kind,
                                             .fields = to.patch->fields});
        }
        dropUnusedMarks(scene);
        return {};
    }
    }
    return deltaInvalid("a delta's kind is one of the closed set");
}

} // namespace

result::Status apply(scene::Scene& scene, const Delta& delta, bool forward) {
    if (!shaped(delta)) {
        return deltaInvalid("a delta holds its kind's slot, before and after");
    }
    const SlotValue& from = forward ? delta.before : delta.after;
    const SlotValue& to = forward ? delta.after : delta.before;
    const std::optional<SlotValue> kNow = slotOf(scene, delta);
    if (!kNow.has_value() || *kNow != from) {
        return deltaMismatch("a delta applies to a slot holding what it leaves from");
    }
    scene::Scene changed = scene;
    RAWFRAME_TRY(writeSlot(changed, delta, to));
    scene = std::move(changed);
    return {};
}

result::Status apply(scene::Scene& scene, const Journal& journal, bool forward) {
    scene::Scene changed = scene;
    for (std::size_t at = 0; at < journal.size(); ++at) {
        const Delta& delta = forward ? journal[at] : journal[journal.size() - 1 - at];
        RAWFRAME_TRY(apply(changed, delta, forward));
    }
    scene = std::move(changed);
    return {};
}

} // namespace rawframe::authoring
