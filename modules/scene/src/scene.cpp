#include "rawframe/scene/scene.h"

#include "rawframe/document/json.h"
#include "rawframe/scene/errors.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <tuple>

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

/// A number's text as the profile writes it.
bool canonicalNumber(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value() || parsed->kind() != Value::Kind::Number) {
        return false;
    }
    std::string written = document::write(*parsed);
    written.pop_back();
    return written == text;
}

template <typename Named> bool inNameOrder(const std::vector<Named>& named, std::string Named::* name) {
    for (std::size_t at = 0; at < named.size(); ++at) {
        if ((named[at].*name).empty() || (at > 0 && !(named[at - 1].*name < named[at].*name))) {
            return false;
        }
    }
    return true;
}

/// Fields in name order, each value in its form; a default only where
/// `defaults` allows it.
bool fieldsInForm(const std::vector<SceneField>& fields, bool defaults) {
    if (fields.size() > kMaximumFields || !inNameOrder(fields, &SceneField::name)) {
        return false;
    }
    return std::ranges::all_of(fields, [defaults](const SceneField& field) {
        switch (field.value.kind) {
        case FieldValue::Kind::Number:
            return canonicalNumber(field.value.number) && (defaults || field.value.number != "0");
        case FieldValue::Kind::False:
            return defaults;
        case FieldValue::Kind::True:
        case FieldValue::Kind::Entity:
            return true;
        }
        return false;
    });
}

result::Status validate(const Scene& scene) {
    if (scene.entities.size() > kMaximumEntities || scene.instances.size() > kMaximumInstances) {
        return invalid("a scene holds at most 65,536 entities and 4,096 instances");
    }
    if (!inNameOrder(scene.schema, &SchemaMark::component)) {
        return invalid("a scene's schema names each component once, in name order");
    }
    // Every id the scene holds: its entities', and its instances'.
    std::vector<base::Bits128> held;
    std::vector<std::string> used;
    std::vector<const SceneField*> references;
    const auto kReferences = [&references](const std::vector<SceneField>& fields) {
        for (const SceneField& field : fields) {
            if (field.value.kind == FieldValue::Kind::Entity) {
                references.push_back(&field);
            }
        }
    };
    for (const SceneEntity& entity : scene.entities) {
        held.push_back(entity.id);
        if (entity.components.size() > kMaximumComponents || !inNameOrder(entity.components, &SceneComponent::name)) {
            return invalid("an entity's components are named once each, in name order, at most 256");
        }
        for (const SceneComponent& component : entity.components) {
            used.push_back(component.name);
            if (!fieldsInForm(component.fields, false)) {
                return invalid("a component's fields are named once each, in name order, at most 1,024, each in "
                               "its canonical text and none at its default");
            }
            kReferences(component.fields);
        }
    }
    for (const SceneInstance& instance : scene.instances) {
        if (instance.scene == base::Bits128{} || instance.entities.size() > kMaximumEntities ||
            instance.overrides.size() > kMaximumOverrides) {
            return invalid("an instance names its scene and at most 65,536 entities and overrides");
        }
        for (std::size_t at = 0; at < instance.entities.size(); ++at) {
            const IdentityMapping& mapping = instance.entities[at];
            if (mapping.source == base::Bits128{} || (at > 0 && !(instance.entities[at - 1].source < mapping.source))) {
                return invalid("an instance maps each of its scene's entities once, in the order of their ids");
            }
            held.push_back(mapping.instance);
        }
        for (std::size_t at = 0; at < instance.overrides.size(); ++at) {
            const Override& change = instance.overrides[at];
            const bool kOrdered =
                at == 0 || std::tie(instance.overrides[at - 1].entity, instance.overrides[at - 1].component) <
                               std::tie(change.entity, change.component);
            const bool kMapped = std::ranges::contains(instance.entities, change.entity, &IdentityMapping::instance);
            const bool kInForm = change.kind == Override::Kind::Remove
                                     ? change.fields.empty()
                                     : fieldsInForm(change.fields, change.kind == Override::Kind::Set) &&
                                           !(change.kind == Override::Kind::Set && change.fields.empty());
            // An entity removed has that entry alone.
            const bool kWhole = change.component.empty();
            const bool kAlone =
                !kWhole ||
                ((at + 1 == instance.overrides.size() || instance.overrides[at + 1].entity != change.entity) &&
                 change.kind == Override::Kind::Remove);
            if (!kOrdered || !kMapped || !kAlone || !kInForm) {
                return invalid("an override names an entity of its instance and a component, once, in order, and "
                               "changes it in the one form of its kind, or removes the entity alone");
            }
            if (!kWhole) {
                used.push_back(change.component);
            }
            kReferences(change.fields);
        }
    }
    std::ranges::sort(held);
    if (std::ranges::contains(held, base::Bits128{}) || std::ranges::adjacent_find(held) != held.end()) {
        return invalid("an entity's id is not nought, and is its own among everything the scene holds");
    }
    for (const SceneField* field : references) {
        if (!std::ranges::binary_search(held, field->value.entity)) {
            return invalid("an entity reference names an entity the scene holds");
        }
    }
    std::ranges::sort(used);
    const auto kRepeated = std::ranges::unique(used);
    used.erase(kRepeated.begin(), kRepeated.end());
    if (!std::ranges::equal(used, scene.schema, {}, {}, &SchemaMark::component)) {
        return invalid("a scene's schema is exactly the components its entities and overrides use");
    }
    return {};
}

/// The object members of `value`, which must be exactly `names` in order.
bool hasMembers(const Value& value, std::initializer_list<std::string_view> names) {
    return value.kind() == Value::Kind::Object && std::ranges::equal(value.names(), names);
}

/// A field's value; `false` only where a default may be written.
result::Result<FieldValue> fieldOf(const Value& value, bool defaults) {
    switch (value.kind()) {
    case Value::Kind::Number:
        return FieldValue{.kind = FieldValue::Kind::Number, .number = *value.text()};
    case Value::Kind::Bool:
        if (*value.truth()) {
            return FieldValue{.kind = FieldValue::Kind::True};
        }
        if (defaults) {
            return FieldValue{.kind = FieldValue::Kind::False};
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

result::Result<std::vector<SceneField>> fieldsOf(const Value& fields, bool defaults) {
    if (fields.kind() != Value::Kind::Object || fields.names().size() > kMaximumFields) {
        return invalid("a component is an object of its fields");
    }
    std::vector<SceneField> made;
    for (std::size_t at = 0; at < fields.names().size(); ++at) {
        RAWFRAME_TRY_ASSIGN(FieldValue value, fieldOf(fields.items()[at], defaults));
        made.push_back(SceneField{.name = fields.names()[at], .value = std::move(value)});
    }
    return made;
}

Value valueOf(const std::vector<SceneField>& fields) {
    Value made = Value::object();
    for (const SceneField& field : fields) {
        switch (field.value.kind) {
        case FieldValue::Kind::Number:
            made.add(field.name, Value::numberText(field.value.number));
            break;
        case FieldValue::Kind::True:
            made.add(field.name, Value::boolean(true));
            break;
        case FieldValue::Kind::False:
            made.add(field.name, Value::boolean(false));
            break;
        case FieldValue::Kind::Entity:
            made.add(field.name, Value::string(idText(field.value.entity)));
            break;
        }
    }
    return made;
}

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

result::Result<base::Bits128> idOf(const Value* value) {
    const base::Bits128Parse kId =
        value != nullptr && value->text() != nullptr ? schema::parseStableIdText(*value->text()) : base::Bits128Parse{};
    if (!kId.parsed) {
        return invalid("an entity's id is a UUID in canonical text");
    }
    return kId.value;
}

result::Result<SceneInstance> instanceOf(const Value& each) {
    const bool kShape = hasMembers(each, {"scene", "entities", "overrides"});
    const base::Bits128Parse kScene = kShape && each.find("scene")->text() != nullptr
                                          ? base::parseBits128Hex(*each.find("scene")->text())
                                          : base::Bits128Parse{};
    if (!kShape || !kScene.parsed || hexOf(kScene.value) != *each.find("scene")->text() ||
        each.find("entities")->kind() != Value::Kind::Object || each.find("overrides")->kind() != Value::Kind::Array) {
        return invalid("an instance is its scene, its entities, and its overrides");
    }
    SceneInstance instance{.scene = kScene.value};
    const Value& entities = *each.find("entities");
    for (std::size_t at = 0; at < entities.names().size(); ++at) {
        const base::Bits128Parse kSource = schema::parseStableIdText(entities.names()[at]);
        RAWFRAME_TRY_ASSIGN(const base::Bits128 kInstance, idOf(&entities.items()[at]));
        if (!kSource.parsed) {
            return invalid("an instance maps its scene's entities by their ids");
        }
        instance.entities.push_back(IdentityMapping{.source = kSource.value, .instance = kInstance});
    }
    for (const Value& change : each.find("overrides")->items()) {
        const Value* set = change.find("set");
        const Value* add = change.find("add");
        const Value* remove = change.find("remove");
        if (hasMembers(change, {"entity", "remove"})) {
            if (remove->truth() != true) {
                return invalid("a removal is `remove: true`");
            }
            Override removed{.kind = Override::Kind::Remove};
            RAWFRAME_TRY_ASSIGN(removed.entity, idOf(change.find("entity")));
            instance.overrides.push_back(std::move(removed));
            continue;
        }
        const std::string_view kVerb = set != nullptr ? "set" : add != nullptr ? "add" : "remove";
        if (!hasMembers(change, {"entity", "component", kVerb}) || change.find("component")->text() == nullptr) {
            return invalid("an override is an entity, a component, and one of set, add, or remove");
        }
        Override made;
        RAWFRAME_TRY_ASSIGN(made.entity, idOf(change.find("entity")));
        made.component = *change.find("component")->text();
        if (set != nullptr || add != nullptr) {
            made.kind = set != nullptr ? Override::Kind::Set : Override::Kind::Add;
            RAWFRAME_TRY_ASSIGN(made.fields, fieldsOf(set != nullptr ? *set : *add, set != nullptr));
        } else {
            if (remove->truth() != true) {
                return invalid("a removal is `remove: true`");
            }
            made.kind = Override::Kind::Remove;
        }
        instance.overrides.push_back(std::move(made));
    }
    return instance;
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
            components.add(component.name, valueOf(component.fields));
        }
        made.add("components", std::move(components));
        entities.push(std::move(made));
    }
    Value instances = Value::array();
    for (const SceneInstance& instance : scene.instances) {
        Value mapping = Value::object();
        for (const IdentityMapping& each : instance.entities) {
            mapping.add(idText(each.source), Value::string(idText(each.instance)));
        }
        Value overrides = Value::array();
        for (const Override& change : instance.overrides) {
            Value made = Value::object();
            made.add("entity", Value::string(idText(change.entity)));
            if (change.component.empty()) {
                made.add("remove", Value::boolean(true));
                overrides.push(std::move(made));
                continue;
            }
            made.add("component", Value::string(change.component));
            switch (change.kind) {
            case Override::Kind::Set:
                made.add("set", valueOf(change.fields));
                break;
            case Override::Kind::Add:
                made.add("add", valueOf(change.fields));
                break;
            case Override::Kind::Remove:
                made.add("remove", Value::boolean(true));
                break;
            }
            overrides.push(std::move(made));
        }
        Value made = Value::object();
        made.add("scene", Value::string(hexOf(instance.scene)));
        made.add("entities", std::move(mapping));
        made.add("overrides", std::move(overrides));
        instances.push(std::move(made));
    }
    Value document = Value::object();
    document.add("kind", Value::string("rawframe.scene"));
    document.add("formatVersion", Value::integer(1));
    document.add("schema", std::move(marks));
    document.add("entities", std::move(entities));
    if (!scene.instances.empty()) {
        document.add("instances", std::move(instances));
    }
    return document::write(document);
}

result::Result<Scene> readScene(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const bool kInstanced = parsed->find("instances") != nullptr;
    if (!(kInstanced ? hasMembers(*parsed, {"kind", "formatVersion", "schema", "entities", "instances"})
                     : hasMembers(*parsed, {"kind", "formatVersion", "schema", "entities"})) ||
        kind->text() == nullptr || *kind->text() != "rawframe.scene" || version->integer() != 1) {
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
            RAWFRAME_TRY_ASSIGN(std::vector<SceneField> fields, fieldsOf(components.items()[at], false));
            entity.components.push_back(SceneComponent{.name = components.names()[at], .fields = std::move(fields)});
        }
        scene.entities.push_back(std::move(entity));
    }
    if (kInstanced) {
        const Value& instances = *parsed->find("instances");
        if (instances.kind() != Value::Kind::Array || instances.items().empty() ||
            instances.items().size() > kMaximumInstances) {
            return invalid("a scene's instances are an array of one to 4,096, or are left out");
        }
        for (const Value& each : instances.items()) {
            RAWFRAME_TRY_ASSIGN(SceneInstance instance, instanceOf(each));
            scene.instances.push_back(std::move(instance));
        }
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
