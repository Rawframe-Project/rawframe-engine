// Operations on the entities a scene's instances bring (D154): each
// becomes a change to one entry of the instance's patch, whole before and
// whole after, so one operation is at most one delta per entry.

#include "operation_parts.h"

#include <algorithm>
#include <utility>

namespace rawframe::authoring {

namespace {

using PatchKind = scene::Override::Kind;

const scene::Override* entryOf(const scene::SceneInstance& instance, base::Bits128 entity, std::string_view component) {
    const auto kFound = std::ranges::find_if(instance.overrides, [entity, component](const scene::Override& each) {
        return each.entity == entity && each.component == component;
    });
    return kFound != instance.overrides.end() ? &*kFound : nullptr;
}

std::optional<PatchRecord> recordOf(const scene::Scene& scene, const scene::Override* entry) {
    if (entry == nullptr) {
        return std::nullopt;
    }
    return PatchRecord{.kind = entry->kind,
                       .mark = entry->component.empty() ? std::nullopt : std::optional{markOf(scene, entry->component)},
                       .fields = entry->fields};
}

Delta entryDelta(base::Bits128 entity,
                 std::string component,
                 std::optional<PatchRecord> before,
                 std::optional<PatchRecord> after) {
    return Delta{.kind = DeltaKind::SetOverride,
                 .entity = entity,
                 .component = std::move(component),
                 .before = {.patch = std::move(before)},
                 .after = {.patch = std::move(after)}};
}

/// `fields` with `name` holding `value`, or without it for none, in name
/// order.
std::vector<scene::SceneField> withField(std::vector<scene::SceneField> fields,
                                         const std::string& name,
                                         const std::optional<scene::FieldValue>& value) {
    std::erase_if(fields, [&name](const scene::SceneField& each) {
        return each.name == name;
    });
    if (value.has_value()) {
        const auto kAt = std::ranges::lower_bound(fields, name, {}, &scene::SceneField::name);
        fields.insert(kAt, scene::SceneField{.name = name, .value = *value});
    }
    return fields;
}

/// A default written out, as a `set` entry gives it: a patch that sets a
/// field to its default is a change from the source's value.
scene::FieldValue writtenDefault(FieldKind kind) {
    if (kind == FieldKind::Truth) {
        return scene::FieldValue{.kind = scene::FieldValue::Kind::False};
    }
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = "0"};
}

} // namespace

result::Result<Journal> derivePatch(const scene::Scene& scene,
                                    const Operation& operation,
                                    const ComponentCatalog& catalog,
                                    const scene::SceneInstance& instance,
                                    base::Bits128 entity) {
    Journal journal;
    const scene::Override* removal = entryOf(instance, entity, "");
    if (std::holds_alternative<RestoreEntity>(operation)) {
        if (removal == nullptr) {
            return notFound(operation, "the instance does not remove that entity");
        }
        journal.push_back(entryDelta(entity, "", recordOf(scene, removal), std::nullopt));
        return journal;
    }
    if (removal != nullptr) {
        return notFound(operation, "the instance removes that entity");
    }
    if (std::holds_alternative<RenameEntity>(operation) || std::holds_alternative<MoveEntity>(operation)) {
        return invalid(operation, "an instance's entity is named and placed by its source scene");
    }
    if (std::holds_alternative<DestroyEntity>(operation)) {
        if (referenced(scene, entity)) {
            return conflict(operation, "an entity another names is not destroyed");
        }
        // Its removal is its only entry (D120).
        for (const scene::Override& each : instance.overrides) {
            if (each.entity == entity) {
                journal.push_back(entryDelta(entity, each.component, recordOf(scene, &each), std::nullopt));
            }
        }
        journal.push_back(
            entryDelta(entity, "", std::nullopt, PatchRecord{.kind = PatchKind::Remove, .mark = {}, .fields = {}}));
        return journal;
    }

    // Every other operation names a component of the entity.
    const schema::ComponentTypeId kComponent = std::visit(
        [](const auto& each) -> schema::ComponentTypeId {
            if constexpr (requires { each.component; }) {
                return each.component;
            } else {
                return {};
            }
        },
        operation);
    const ComponentSchema* component = catalog.find(kComponent);
    if (component == nullptr) {
        return notFound(operation, "the catalog has no such component");
    }
    RAWFRAME_TRY(sameLayout(scene, operation, *component));
    const scene::Override* entry = entryOf(instance, entity, component->name);
    const std::optional<PatchRecord> kBefore = recordOf(scene, entry);
    const auto kChange = [&](std::optional<PatchRecord> after) {
        if (after != kBefore) {
            journal.push_back(entryDelta(entity, component->name, kBefore, std::move(after)));
        }
    };
    const auto kEntry = [component](PatchKind kind, std::vector<scene::SceneField> fields) {
        return std::optional{PatchRecord{.kind = kind, .mark = component->mark, .fields = std::move(fields)}};
    };
    const bool kRemoved = entry != nullptr && entry->kind == PatchKind::Remove;
    const bool kAdded = entry != nullptr && entry->kind == PatchKind::Add;

    if (std::holds_alternative<RevertComponent>(operation)) {
        if (entry == nullptr) {
            return notFound(operation, "the instance does nothing to that component");
        }
        kChange(std::nullopt);
        return journal;
    }
    if (std::holds_alternative<AddComponent>(operation)) {
        if (kRemoved) {
            return conflict(operation, "the instance removes that component; revert it for the source's");
        }
        if (entry != nullptr) {
            return conflict(operation, "the entity already has that component");
        }
        kChange(kEntry(PatchKind::Add, {}));
        return journal;
    }
    if (std::holds_alternative<RemoveComponent>(operation)) {
        if (kRemoved) {
            return notFound(operation, "the instance removes that component");
        }
        // One the instance added goes with its entry; one the source holds
        // is removed by one.
        kChange(kAdded ? std::nullopt : kEntry(PatchKind::Remove, {}));
        return journal;
    }

    // A field: set, set as a reference, or reverted.
    if (kRemoved) {
        return notFound(operation, "the instance removes that component");
    }
    const std::string fieldName = std::visit(
        [](const auto& each) -> std::string {
            if constexpr (requires { each.field; }) {
                return each.field;
            } else {
                return {};
            }
        },
        operation);
    const auto kField = std::ranges::find(component->fields, fieldName, &FieldSchema::name);
    if (kField == component->fields.end()) {
        return notFound(operation, "the component has no such field");
    }
    if (std::holds_alternative<RevertField>(operation)) {
        if (entry == nullptr || entry->kind != PatchKind::Set ||
            !std::ranges::contains(entry->fields, fieldName, &scene::SceneField::name)) {
            return notFound(operation, "the instance gives that field no value of its own");
        }
        std::vector<scene::SceneField> left = withField(entry->fields, fieldName, std::nullopt);
        kChange(left.empty() ? std::nullopt : kEntry(PatchKind::Set, std::move(left)));
        return journal;
    }
    const bool kReference = std::holds_alternative<SetReference>(operation);
    if ((kField->kind == FieldKind::Reference) != kReference) {
        return invalid(operation, "an entity field is set by scene.set_reference, and only it");
    }
    std::optional<scene::FieldValue> value;
    if (kReference) {
        const std::optional<base::Bits128>& target = std::get<SetReference>(operation).target;
        if (target.has_value()) {
            if (!present(scene, *target)) {
                return invalid(operation, "a reference names an entity of the scene");
            }
            value = scene::FieldValue{.kind = scene::FieldValue::Kind::Entity, .entity = *target};
        } else if (!kAdded) {
            // A set entry has no value for a reference to no entity.
            return invalid(operation, "an instance's patch sets a reference only to an entity");
        }
    } else {
        RAWFRAME_TRY_ASSIGN(value, valueOf(operation, std::get<SetField>(operation).value, kField->kind));
        if (!value.has_value() && !kAdded) {
            value = writtenDefault(kField->kind);
        }
    }
    // An added component's fields are in the one form of a component's,
    // defaults left out; a set entry's name each value it gives.
    kChange(kEntry(kAdded ? PatchKind::Add : PatchKind::Set,
                   withField(entry != nullptr ? entry->fields : std::vector<scene::SceneField>{}, fieldName, value)));
    return journal;
}

} // namespace rawframe::authoring
