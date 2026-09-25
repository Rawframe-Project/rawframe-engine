#include "delta_parts.h"
#include "rawframe/authoring/delta.h"
#include "rawframe/document/json.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace rawframe::authoring {

namespace {

using document::Value;

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
    if (slot.mark.has_value()) {
        return Value::string(markText(*slot.mark));
    }
    if (slot.patch.has_value()) {
        Value made = Value::object();
        made.add("fields", fieldsValue(slot.patch->fields));
        made.add("kind", Value::string(std::string{kPatchKindNames[static_cast<std::size_t>(slot.patch->kind)]}));
        if (slot.patch->mark.has_value()) {
            made.add("mark", Value::string(markText(*slot.patch->mark)));
        }
        return made;
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

std::optional<std::uint64_t> markOf(const Value* value) {
    const std::string* text = textOf(value);
    std::uint64_t mark = 0;
    if (text == nullptr || text->size() != 16 ||
        std::from_chars(text->data(), text->data() + text->size(), mark, 16).ptr != text->data() + 16 ||
        markText(mark) != *text) {
        return std::nullopt;
    }
    return mark;
}

std::optional<std::vector<scene::SceneField>> fieldsOf(const Value* fields) {
    if (fields == nullptr || fields->kind() != Value::Kind::Object) {
        return std::nullopt;
    }
    std::vector<scene::SceneField> made;
    for (std::size_t at = 0; at < fields->names().size(); ++at) {
        const std::optional<scene::FieldValue> kValue = fieldOf(fields->items()[at]);
        if (!kValue.has_value()) {
            return std::nullopt;
        }
        made.push_back(scene::SceneField{.name = fields->names()[at], .value = *kValue});
    }
    return made;
}

std::optional<PatchRecord> patchOf(const Value& value) {
    const bool kMarked = value.find("mark") != nullptr;
    const std::string* kind = textOf(value.find("kind"));
    const auto kKind = kind != nullptr ? std::ranges::find(kPatchKindNames, *kind) : kPatchKindNames.end();
    std::optional<std::vector<scene::SceneField>> fields = fieldsOf(value.find("fields"));
    const std::optional<std::uint64_t> kMark = kMarked ? markOf(value.find("mark")) : std::nullopt;
    if (!(kMarked ? hasMembers(value, {"fields", "kind", "mark"}) : hasMembers(value, {"fields", "kind"})) ||
        kKind == kPatchKindNames.end() || !fields.has_value() || (kMarked && !kMark.has_value())) {
        return std::nullopt;
    }
    return PatchRecord{.kind = static_cast<scene::Override::Kind>(kKind - kPatchKindNames.begin()),
                       .mark = kMark,
                       .fields = std::move(*fields)};
}

std::optional<ComponentRecord> componentOf(const Value& value) {
    const std::string* name = textOf(value.find("name"));
    const std::optional<std::uint64_t> kMark = markOf(value.find("mark"));
    std::optional<std::vector<scene::SceneField>> fields = fieldsOf(value.find("fields"));
    if (!hasMembers(value, {"fields", "mark", "name"}) || name == nullptr || !kMark.has_value() ||
        !fields.has_value()) {
        return std::nullopt;
    }
    return ComponentRecord{.name = *name, .mark = *kMark, .fields = std::move(*fields)};
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
    case DeltaKind::SetMark:
        slot.mark = markOf(&value);
        return slot.mark.has_value() ? std::optional{slot} : std::nullopt;
    case DeltaKind::SetOverride:
        slot.patch = patchOf(value);
        return slot.patch.has_value() ? std::optional{slot} : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

result::Result<std::string> writeJournal(const Journal& journal) {
    Value deltas = Value::array();
    for (const Delta& delta : journal) {
        if (!shaped(delta)) {
            return deltaInvalid("a delta holds its kind's slot, before and after");
        }
        Value made = Value::object();
        made.add("after", slotValue(delta.after));
        made.add("before", slotValue(delta.before));
        if (componentKind(delta.kind) || delta.kind == DeltaKind::SetOverride) {
            made.add("component", Value::string(delta.component));
        }
        if (delta.kind != DeltaKind::SetMark) {
            made.add("entity", Value::string(idText(delta.entity)));
        }
        if (fieldKind(delta.kind)) {
            made.add("field", Value::string(delta.field));
        }
        made.add("kind", Value::string(std::string{kDeltaKindNames[static_cast<std::size_t>(delta.kind)]}));
        deltas.push(std::move(made));
    }
    Value record = Value::object();
    record.add("deltas", std::move(deltas));
    record.add("formatVersion", Value::integer(1));
    record.add("kind", Value::string("authoring.journal"));
    auto written = document::writeCanonicalRecord(record);
    if (!written.has_value()) {
        return deltaInvalid("a journal's text is not what a canonical record can hold");
    }
    return written;
}

result::Result<Journal> readJournal(std::string_view bytes) {
    auto parsed = document::parseCanonicalRecord(bytes);
    if (!parsed.has_value()) {
        return deltaInvalid("a journal is a canonical record");
    }
    const Value* deltas = parsed->find("deltas");
    const Value* version = parsed->find("formatVersion");
    if (!hasMembers(*parsed, {"deltas", "formatVersion", "kind"}) || textOf(parsed->find("kind")) == nullptr ||
        *parsed->find("kind")->text() != "authoring.journal" || version->integer() != 1 ||
        deltas->kind() != Value::Kind::Array) {
        return deltaInvalid("a journal is authoring.journal, format 1, and its deltas");
    }
    Journal journal;
    for (const Value& each : deltas->items()) {
        const std::string* kindName = textOf(each.find("kind"));
        const auto kKind = kindName != nullptr ? std::ranges::find(kDeltaKindNames, *kindName) : kDeltaKindNames.end();
        if (kKind == kDeltaKindNames.end()) {
            return deltaInvalid("a delta's kind is one of the closed set");
        }
        Delta delta{.kind = static_cast<DeltaKind>(kKind - kDeltaKindNames.begin())};
        const bool kComponent = componentKind(delta.kind) || delta.kind == DeltaKind::SetOverride;
        const bool kField = fieldKind(delta.kind);
        const bool kMark = delta.kind == DeltaKind::SetMark;
        const bool kShape = kMark        ? hasMembers(each, {"after", "before", "component", "kind"})
                            : kField     ? hasMembers(each, {"after", "before", "component", "entity", "field", "kind"})
                            : kComponent ? hasMembers(each, {"after", "before", "component", "entity", "kind"})
                                         : hasMembers(each, {"after", "before", "entity", "kind"});
        const std::optional<base::Bits128> kEntity = !kShape ? std::nullopt
                                                     : kMark ? std::optional{base::Bits128{}}
                                                             : idOf(each.find("entity"));
        if (!kEntity.has_value() || (kComponent && textOf(each.find("component")) == nullptr) ||
            (kField && textOf(each.find("field")) == nullptr)) {
            return deltaInvalid("a delta is its kind, entity, component and field as its kind has them, and slots");
        }
        delta.entity = *kEntity;
        delta.component = kComponent ? *each.find("component")->text() : "";
        delta.field = kField ? *each.find("field")->text() : "";
        std::optional<SlotValue> before = slotOf(*each.find("before"), delta.kind);
        std::optional<SlotValue> after = slotOf(*each.find("after"), delta.kind);
        if (!before.has_value() || !after.has_value()) {
            return deltaInvalid("a delta's slots hold what its kind holds");
        }
        delta.before = std::move(*before);
        delta.after = std::move(*after);
        if (!shaped(delta)) {
            return deltaInvalid("a delta holds its kind's slot, before and after");
        }
        journal.push_back(std::move(delta));
    }
    // What the writer makes of it is the text, byte for byte.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeJournal(journal));
    if (kWritten != bytes) {
        return deltaInvalid("a journal is not in its canonical form");
    }
    return journal;
}

} // namespace rawframe::authoring
