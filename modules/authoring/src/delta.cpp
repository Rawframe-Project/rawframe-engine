#include "rawframe/authoring/delta.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::DeltaInvalid), why);
}

std::unexpected<result::Error> mismatch(std::string_view why) {
    return result::fail(
        result::ErrorClass::FailedPrecondition, kAuthoringDomain, code(AuthoringError::DeltaMismatch), why);
}

constexpr std::array<std::string_view, 8> kKindNames = {"create_node",
                                                        "destroy_node",
                                                        "reorder",
                                                        "set_name",
                                                        "add_component",
                                                        "remove_component",
                                                        "set_field",
                                                        "set_reference"};

bool componentKind(DeltaKind kind) {
    return kind == DeltaKind::AddComponent || kind == DeltaKind::RemoveComponent || kind == DeltaKind::SetField ||
           kind == DeltaKind::SetReference;
}

bool fieldKind(DeltaKind kind) {
    return kind == DeltaKind::SetField || kind == DeltaKind::SetReference;
}

/// Only the member the kind uses, or none.
bool holdsOnly(const SlotValue& slot, DeltaKind kind) {
    const bool kNode = kind == DeltaKind::CreateNode || kind == DeltaKind::DestroyNode;
    const bool kComponent = kind == DeltaKind::AddComponent || kind == DeltaKind::RemoveComponent;
    return (kNode || !slot.node.has_value()) && (kind == DeltaKind::Reorder || !slot.place.has_value()) &&
           (kind == DeltaKind::SetName || !slot.name.has_value()) && (kComponent || !slot.component.has_value()) &&
           (fieldKind(kind) || !slot.field.has_value());
}

bool fieldOfKind(const std::optional<scene::FieldValue>& value, bool reference) {
    if (!value.has_value()) {
        return true;
    }
    return reference ? value->kind == scene::FieldValue::Kind::Entity
                     : value->kind == scene::FieldValue::Kind::Number || value->kind == scene::FieldValue::Kind::True;
}

/// The delta holds its kind's shape.
bool shaped(const Delta& delta) {
    if (delta.entity == base::Bits128{} || !holdsOnly(delta.before, delta.kind) ||
        !holdsOnly(delta.after, delta.kind) || componentKind(delta.kind) == delta.component.empty() ||
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
    }
    return false;
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
                return mismatch("an entity is made at a place the scene has");
            }
            scene::SceneEntity made{.id = delta.entity, .name = to.node->name, .components = {}};
            for (const ComponentRecord& component : to.node->components) {
                if (!useMark(scene, component.name, component.mark)) {
                    return mismatch("a component's mark is the one the scene's schema holds");
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
            return mismatch("an entity moves to a place the scene has");
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
                return mismatch("a component's mark is the one the scene's schema holds");
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
    }
    return invalid("a delta's kind is one of the closed set");
}

} // namespace

result::Status apply(scene::Scene& scene, const Delta& delta, bool forward) {
    if (!shaped(delta)) {
        return invalid("a delta holds its kind's slot, before and after");
    }
    const SlotValue& from = forward ? delta.before : delta.after;
    const SlotValue& to = forward ? delta.after : delta.before;
    const std::optional<SlotValue> kNow = slotOf(scene, delta);
    if (!kNow.has_value() || *kNow != from) {
        return mismatch("a delta applies to a slot holding what it leaves from");
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

namespace {

std::string idText(base::Bits128 id) {
    const auto kText = schema::formatStableIdText(id);
    return std::string{kText.data(), kText.size()};
}

std::string markText(std::uint64_t mark) {
    std::array<char, 16> digits{};
    const char* const kEnd = std::to_chars(digits.data(), digits.data() + digits.size(), mark, 16).ptr;
    std::string text(digits.size() - static_cast<std::size_t>(kEnd - digits.data()), '0');
    text.append(std::string_view{digits.data(), kEnd});
    return text;
}

Value fieldValue(const scene::FieldValue& value) {
    switch (value.kind) {
    case scene::FieldValue::Kind::Number: {
        Value made = Value::object();
        made.add("number", Value::string(value.number));
        return made;
    }
    case scene::FieldValue::Kind::Entity: {
        Value made = Value::object();
        made.add("entity", Value::string(idText(value.entity)));
        return made;
    }
    case scene::FieldValue::Kind::True:
        return Value::boolean(true);
    case scene::FieldValue::Kind::False:
        return Value::boolean(false);
    }
    return {};
}

Value fieldsValue(const std::vector<scene::SceneField>& fields) {
    Value made = Value::object();
    for (const scene::SceneField& field : fields) {
        made.add(field.name, fieldValue(field.value));
    }
    return made;
}

Value componentValue(const ComponentRecord& component) {
    Value made = Value::object();
    made.add("fields", fieldsValue(component.fields));
    made.add("mark", Value::string(markText(component.mark)));
    made.add("name", Value::string(component.name));
    return made;
}

Value slotValue(const SlotValue& slot) {
    if (slot.node.has_value()) {
        Value components = Value::array();
        for (const ComponentRecord& component : slot.node->components) {
            components.push(componentValue(component));
        }
        Value made = Value::object();
        made.add("components", std::move(components));
        made.add("name", Value::string(slot.node->name));
        made.add("place", Value::integer(static_cast<std::int64_t>(slot.node->place)));
        return made;
    }
    if (slot.place.has_value()) {
        return Value::integer(static_cast<std::int64_t>(*slot.place));
    }
    if (slot.name.has_value()) {
        return Value::string(*slot.name);
    }
    if (slot.component.has_value()) {
        return componentValue(*slot.component);
    }
    if (slot.field.has_value()) {
        return fieldValue(*slot.field);
    }
    return {};
}

bool hasMembers(const Value& value, std::initializer_list<std::string_view> names) {
    return value.kind() == Value::Kind::Object && value.names().size() == names.size() &&
           std::ranges::all_of(names, [&value](std::string_view name) {
               return value.find(name) != nullptr;
           });
}

const std::string* textOf(const Value* value) {
    return value != nullptr && value->kind() == Value::Kind::String ? value->text() : nullptr;
}

std::optional<base::Bits128> idOf(const Value* value) {
    const std::string* text = textOf(value);
    if (text == nullptr) {
        return std::nullopt;
    }
    const base::Bits128Parse kParsed = schema::parseStableIdText(*text);
    return kParsed.parsed ? std::optional{kParsed.value} : std::nullopt;
}

std::optional<std::size_t> placeValue(const Value* value) {
    const std::optional<std::int64_t> kPlace =
        value != nullptr && value->kind() == Value::Kind::Number ? value->integer() : std::nullopt;
    return kPlace.has_value() && *kPlace >= 0 ? std::optional{static_cast<std::size_t>(*kPlace)} : std::nullopt;
}

std::optional<scene::FieldValue> fieldOf(const Value& value) {
    if (value.kind() == Value::Kind::Bool) {
        return scene::FieldValue{.kind =
                                     *value.truth() ? scene::FieldValue::Kind::True : scene::FieldValue::Kind::False};
    }
    if (hasMembers(value, {"number"}) && textOf(value.find("number")) != nullptr) {
        return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = *value.find("number")->text()};
    }
    if (hasMembers(value, {"entity"})) {
        const std::optional<base::Bits128> kEntity = idOf(value.find("entity"));
        if (kEntity.has_value()) {
            return scene::FieldValue{.kind = scene::FieldValue::Kind::Entity, .entity = *kEntity};
        }
    }
    return std::nullopt;
}

std::optional<ComponentRecord> componentOf(const Value& value) {
    const std::string* name = textOf(value.find("name"));
    const std::string* mark = textOf(value.find("mark"));
    const Value* fields = value.find("fields");
    if (!hasMembers(value, {"fields", "mark", "name"}) || name == nullptr || mark == nullptr || mark->size() != 16 ||
        fields->kind() != Value::Kind::Object) {
        return std::nullopt;
    }
    ComponentRecord made{.name = *name, .mark = 0, .fields = {}};
    const auto kParsed = std::from_chars(mark->data(), mark->data() + mark->size(), made.mark, 16);
    if (kParsed.ec != std::errc{} || kParsed.ptr != mark->data() + mark->size() || markText(made.mark) != *mark) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at < fields->names().size(); ++at) {
        const std::optional<scene::FieldValue> kValue = fieldOf(fields->items()[at]);
        if (!kValue.has_value()) {
            return std::nullopt;
        }
        made.fields.push_back(scene::SceneField{.name = fields->names()[at], .value = *kValue});
    }
    return made;
}

std::optional<SlotValue> slotOf(const Value& value, DeltaKind kind) {
    SlotValue slot;
    if (value.isNull()) {
        return slot;
    }
    switch (kind) {
    case DeltaKind::CreateNode:
    case DeltaKind::DestroyNode: {
        const std::string* name = textOf(value.find("name"));
        const std::optional<std::size_t> kPlace = placeValue(value.find("place"));
        const Value* components = value.find("components");
        if (!hasMembers(value, {"components", "name", "place"}) || name == nullptr || !kPlace.has_value() ||
            components->kind() != Value::Kind::Array) {
            return std::nullopt;
        }
        NodeRecord node{.place = *kPlace, .name = *name, .components = {}};
        for (const Value& each : components->items()) {
            std::optional<ComponentRecord> component = componentOf(each);
            if (!component.has_value()) {
                return std::nullopt;
            }
            node.components.push_back(std::move(*component));
        }
        slot.node = std::move(node);
        return slot;
    }
    case DeltaKind::Reorder:
        slot.place = placeValue(&value);
        return slot.place.has_value() ? std::optional{slot} : std::nullopt;
    case DeltaKind::SetName:
        if (textOf(&value) == nullptr) {
            return std::nullopt;
        }
        slot.name = *value.text();
        return slot;
    case DeltaKind::AddComponent:
    case DeltaKind::RemoveComponent:
        slot.component = componentOf(value);
        return slot.component.has_value() ? std::optional{slot} : std::nullopt;
    case DeltaKind::SetField:
    case DeltaKind::SetReference:
        slot.field = fieldOf(value);
        return slot.field.has_value() ? std::optional{slot} : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

result::Result<std::string> writeJournal(const Journal& journal) {
    Value deltas = Value::array();
    for (const Delta& delta : journal) {
        if (!shaped(delta)) {
            return invalid("a delta holds its kind's slot, before and after");
        }
        Value made = Value::object();
        made.add("after", slotValue(delta.after));
        made.add("before", slotValue(delta.before));
        if (componentKind(delta.kind)) {
            made.add("component", Value::string(delta.component));
        }
        made.add("entity", Value::string(idText(delta.entity)));
        if (fieldKind(delta.kind)) {
            made.add("field", Value::string(delta.field));
        }
        made.add("kind", Value::string(std::string{kKindNames[static_cast<std::size_t>(delta.kind)]}));
        deltas.push(std::move(made));
    }
    Value record = Value::object();
    record.add("deltas", std::move(deltas));
    record.add("formatVersion", Value::integer(1));
    record.add("kind", Value::string("authoring.journal"));
    auto written = document::writeCanonicalRecord(record);
    if (!written.has_value()) {
        return invalid("a journal's text is not what a canonical record can hold");
    }
    return written;
}

result::Result<Journal> readJournal(std::string_view bytes) {
    auto parsed = document::parseCanonicalRecord(bytes);
    if (!parsed.has_value()) {
        return invalid("a journal is a canonical record");
    }
    const Value* deltas = parsed->find("deltas");
    const Value* version = parsed->find("formatVersion");
    if (!hasMembers(*parsed, {"deltas", "formatVersion", "kind"}) || textOf(parsed->find("kind")) == nullptr ||
        *parsed->find("kind")->text() != "authoring.journal" || version->integer() != 1 ||
        deltas->kind() != Value::Kind::Array) {
        return invalid("a journal is authoring.journal, format 1, and its deltas");
    }
    Journal journal;
    for (const Value& each : deltas->items()) {
        const std::string* kindName = textOf(each.find("kind"));
        const auto kKind = kindName != nullptr ? std::ranges::find(kKindNames, *kindName) : kKindNames.end();
        if (kKind == kKindNames.end()) {
            return invalid("a delta's kind is one of the closed set");
        }
        Delta delta{.kind = static_cast<DeltaKind>(kKind - kKindNames.begin())};
        const bool kComponent = componentKind(delta.kind);
        const bool kField = fieldKind(delta.kind);
        const bool kShape = kField       ? hasMembers(each, {"after", "before", "component", "entity", "field", "kind"})
                            : kComponent ? hasMembers(each, {"after", "before", "component", "entity", "kind"})
                                         : hasMembers(each, {"after", "before", "entity", "kind"});
        const std::optional<base::Bits128> kEntity = kShape ? idOf(each.find("entity")) : std::nullopt;
        if (!kEntity.has_value() || (kComponent && textOf(each.find("component")) == nullptr) ||
            (kField && textOf(each.find("field")) == nullptr)) {
            return invalid("a delta is its kind, entity, component and field as its kind has them, and slots");
        }
        delta.entity = *kEntity;
        delta.component = kComponent ? *each.find("component")->text() : "";
        delta.field = kField ? *each.find("field")->text() : "";
        std::optional<SlotValue> before = slotOf(*each.find("before"), delta.kind);
        std::optional<SlotValue> after = slotOf(*each.find("after"), delta.kind);
        if (!before.has_value() || !after.has_value()) {
            return invalid("a delta's slots hold what its kind holds");
        }
        delta.before = std::move(*before);
        delta.after = std::move(*after);
        if (!shaped(delta)) {
            return invalid("a delta holds its kind's slot, before and after");
        }
        journal.push_back(std::move(delta));
    }
    // What the writer makes of it is the text, byte for byte.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeJournal(journal));
    if (kWritten != bytes) {
        return invalid("a journal is not in its canonical form");
    }
    return journal;
}

} // namespace rawframe::authoring
