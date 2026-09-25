#include "rawframe/scene/scene.h"

#include "rawframe/document/json.h"
#include "rawframe/scene/errors.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace rawframe::scene {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kSceneDomain, code(SceneError::SceneInvalid), why);
}

std::string idText(base::Bits128 id) {
    const auto kText = schema::formatStableIdText(id);
    return std::string{kText.data(), kText.size()};
}

std::string markText(std::uint64_t mark) {
    std::array<char, 16> digits{};
    for (std::size_t at = 0; at < digits.size(); ++at) {
        digits[at] = "0123456789abcdef"[(mark >> (4U * (15U - at))) & 0xFU];
    }
    return std::string{digits.data(), digits.size()};
}

/// A number's text as the profile writes it, and not the default.
bool canonicalNumber(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value() || parsed->kind() != Value::Kind::Number) {
        return false;
    }
    std::string written = document::write(*parsed);
    written.pop_back();
    return written == text && text != "0";
}

template <typename Named> bool inNameOrder(const std::vector<Named>& named, std::string Named::* name) {
    for (std::size_t at = 0; at < named.size(); ++at) {
        if ((named[at].*name).empty() || (at > 0 && !(named[at - 1].*name < named[at].*name))) {
            return false;
        }
    }
    return true;
}

result::Status validate(const Scene& scene) {
    if (scene.entities.size() > kMaximumEntities) {
        return invalid("a scene holds at most 65,536 entities");
    }
    if (!inNameOrder(scene.schema, &SchemaMark::component)) {
        return invalid("a scene's schema names each component once, in name order");
    }
    std::vector<base::Bits128> ids;
    std::vector<std::string> used;
    for (const SceneEntity& entity : scene.entities) {
        if (entity.id == base::Bits128{}) {
            return invalid("an entity's id is not nought");
        }
        ids.push_back(entity.id);
        if (entity.components.size() > kMaximumComponents || !inNameOrder(entity.components, &SceneComponent::name)) {
            return invalid("an entity's components are named once each, in name order, at most 256");
        }
        for (const SceneComponent& component : entity.components) {
            used.push_back(component.name);
            if (component.fields.size() > kMaximumFields || !inNameOrder(component.fields, &SceneField::name)) {
                return invalid("a component's fields are named once each, in name order, at most 1,024");
            }
            for (const SceneField& field : component.fields) {
                if (field.value.kind == FieldValue::Kind::Number && !canonicalNumber(field.value.number)) {
                    return invalid("a number is in its canonical text, and not the default of nought");
                }
            }
        }
    }
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end()) {
        return invalid("an entity's id is its own in the document");
    }
    for (const SceneEntity& entity : scene.entities) {
        for (const SceneComponent& component : entity.components) {
            for (const SceneField& field : component.fields) {
                if (field.value.kind == FieldValue::Kind::Entity &&
                    !std::ranges::binary_search(ids, field.value.entity)) {
                    return invalid("an entity reference names an entity of the document");
                }
            }
        }
    }
    std::ranges::sort(used);
    const auto kRepeated = std::ranges::unique(used);
    used.erase(kRepeated.begin(), kRepeated.end());
    if (!std::ranges::equal(used, scene.schema, {}, {}, &SchemaMark::component)) {
        return invalid("a scene's schema is exactly the components its entities use");
    }
    return {};
}

/// The object members of `value`, which must be exactly `names` in order.
bool hasMembers(const Value& value, std::initializer_list<std::string_view> names) {
    return value.kind() == Value::Kind::Object && std::ranges::equal(value.names(), names);
}

result::Result<FieldValue> fieldOf(const Value& value) {
    switch (value.kind()) {
    case Value::Kind::Number:
        return FieldValue{.kind = FieldValue::Kind::Number, .number = *value.text()};
    case Value::Kind::Bool:
        if (*value.truth()) {
            return FieldValue{.kind = FieldValue::Kind::True};
        }
        return invalid("a field at its default, false, is not written");
    case Value::Kind::String: {
        const base::Bits128Parse kId = schema::parseStableIdText(*value.text());
        if (!kId.parsed) {
            return invalid("a text field is an entity of the document, by its id");
        }
        return FieldValue{.kind = FieldValue::Kind::Entity, .entity = kId.value};
    }
    default:
        return invalid("a field is a number, true, or an entity");
    }
}

} // namespace

result::Result<std::string> writeScene(const Scene& scene) {
    RAWFRAME_TRY(validate(scene));
    Value marks = Value::object();
    for (const SchemaMark& mark : scene.schema) {
        marks.add(mark.component, Value::string(markText(mark.mark)));
    }
    Value entities = Value::array();
    for (const SceneEntity& entity : scene.entities) {
        Value made = Value::object();
        made.add("id", Value::string(idText(entity.id)));
        if (!entity.name.empty()) {
            made.add("name", Value::string(entity.name));
        }
        Value components = Value::object();
        for (const SceneComponent& component : entity.components) {
            Value fields = Value::object();
            for (const SceneField& field : component.fields) {
                switch (field.value.kind) {
                case FieldValue::Kind::Number:
                    fields.add(field.name, Value::numberText(field.value.number));
                    break;
                case FieldValue::Kind::True:
                    fields.add(field.name, Value::boolean(true));
                    break;
                case FieldValue::Kind::Entity:
                    fields.add(field.name, Value::string(idText(field.value.entity)));
                    break;
                }
            }
            components.add(component.name, std::move(fields));
        }
        made.add("components", std::move(components));
        entities.push(std::move(made));
    }
    Value document = Value::object();
    document.add("kind", Value::string("rawframe.scene"));
    document.add("formatVersion", Value::integer(1));
    document.add("schema", std::move(marks));
    document.add("entities", std::move(entities));
    return document::write(document);
}

result::Result<Scene> readScene(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    if (!hasMembers(*parsed, {"kind", "formatVersion", "schema", "entities"}) || kind->text() == nullptr ||
        *kind->text() != "rawframe.scene" || version->integer() != 1) {
        return invalid("a scene is kind rawframe.scene, format version 1, a schema, and entities");
    }
    Scene scene;
    const Value& marks = *parsed->find("schema");
    if (marks.kind() != Value::Kind::Object) {
        return invalid("a scene's schema is an object of layout marks");
    }
    for (std::size_t at = 0; at < marks.names().size(); ++at) {
        const Value& mark = marks.items()[at];
        std::uint64_t value = 0;
        const std::string* digits = mark.text();
        if (digits == nullptr || digits->size() != 16 ||
            std::from_chars(digits->data(), digits->data() + digits->size(), value, 16).ptr !=
                digits->data() + digits->size()) {
            return invalid("a layout mark is sixteen hex digits");
        }
        scene.schema.push_back(SchemaMark{.component = marks.names()[at], .mark = value});
    }
    const Value& entities = *parsed->find("entities");
    if (entities.kind() != Value::Kind::Array || entities.items().size() > kMaximumEntities) {
        return invalid("a scene's entities are an array of at most 65,536");
    }
    for (const Value& each : entities.items()) {
        const bool kNamed = each.find("name") != nullptr;
        if (!(kNamed ? hasMembers(each, {"id", "name", "components"}) : hasMembers(each, {"id", "components"}))) {
            return invalid("an entity is an id, an optional name, and components");
        }
        SceneEntity entity;
        const Value& id = *each.find("id");
        const base::Bits128Parse kId =
            id.text() != nullptr ? schema::parseStableIdText(*id.text()) : base::Bits128Parse{};
        if (!kId.parsed) {
            return invalid("an entity's id is a UUID in canonical text");
        }
        entity.id = kId.value;
        if (kNamed) {
            if (each.find("name")->text() == nullptr || each.find("name")->text()->empty()) {
                return invalid("an entity's name is text, not empty");
            }
            entity.name = *each.find("name")->text();
        }
        const Value& components = *each.find("components");
        if (components.kind() != Value::Kind::Object || components.names().size() > kMaximumComponents) {
            return invalid("an entity's components are an object");
        }
        for (std::size_t at = 0; at < components.names().size(); ++at) {
            const Value& fields = components.items()[at];
            if (fields.kind() != Value::Kind::Object || fields.names().size() > kMaximumFields) {
                return invalid("a component is an object of its fields");
            }
            SceneComponent component{.name = components.names()[at]};
            for (std::size_t field = 0; field < fields.names().size(); ++field) {
                RAWFRAME_TRY_ASSIGN(FieldValue value, fieldOf(fields.items()[field]));
                component.fields.push_back(SceneField{.name = fields.names()[field], .value = std::move(value)});
            }
            entity.components.push_back(std::move(component));
        }
        scene.entities.push_back(std::move(entity));
    }
    // What the writer makes of it is the text, byte for byte, or the text
    // was not in the one form.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeScene(scene));
    if (kWritten != text) {
        return invalid("a scene is not in its canonical form");
    }
    return scene;
}

} // namespace rawframe::scene
