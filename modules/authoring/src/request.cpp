#include "rawframe/authoring/request.h"

#include "operation_parts.h"
#include "rawframe/schema/stable_id.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace rawframe::authoring {

namespace {

using document::Value;

constexpr std::array<std::string_view, 10> kCodeNames = {"validation_failed",
                                                         "target_not_found",
                                                         "target_stale",
                                                         "capability_denied",
                                                         "limit_exceeded",
                                                         "conflict",
                                                         "unsupported_operation",
                                                         "internal",
                                                         "delta_invalid",
                                                         "delta_mismatch"};

constexpr std::array<std::string_view, 8> kInputTypeNames = {
    "entity", "component", "field", "text", "place", "optional_place", "value", "optional_entity"};
constexpr std::array<std::string_view, 4> kHistoryNames = {
    "undoable", "non_dirtying", "bulk_non_undoable", "read_only"};

std::unexpected<result::Error> malformed(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::ValidationFailed), why);
}

const std::string* textOf(const Value* value) {
    return value != nullptr && value->kind() == Value::Kind::String ? value->text() : nullptr;
}

std::optional<base::Bits128> idOf(const Value* value) {
    const std::string* text = textOf(value);
    const base::Bits128Parse kParsed = text != nullptr ? schema::parseStableIdText(*text) : base::Bits128Parse{};
    return kParsed.parsed ? std::optional{kParsed.value} : std::nullopt;
}

std::optional<std::size_t> placeOf(const Value* value) {
    const std::optional<std::int64_t> kPlace =
        value != nullptr && value->kind() == Value::Kind::Number ? value->integer() : std::nullopt;
    return kPlace.has_value() && *kPlace >= 0 ? std::optional{static_cast<std::size_t>(*kPlace)} : std::nullopt;
}

template <typename Integer> std::optional<Integer> integerOf(const Value* value) {
    const std::string* text = textOf(value);
    if (text == nullptr || text->empty()) {
        return std::nullopt;
    }
    Integer made{};
    const auto kParsed = std::from_chars(text->data(), text->data() + text->size(), made);
    if (kParsed.ec != std::errc{} || kParsed.ptr != text->data() + text->size() || std::to_string(made) != *text) {
        return std::nullopt;
    }
    return made;
}

std::optional<FieldInput> fieldInputOf(const Value& value) {
    if (value.isNull()) {
        return FieldInput{};
    }
    if (value.kind() != Value::Kind::Object || value.names().size() != 1) {
        return std::nullopt;
    }
    const std::string& kind = value.names()[0];
    const Value& held = value.items()[0];
    if (kind == "signed") {
        const auto kInteger = integerOf<std::int64_t>(&held);
        return kInteger.has_value() ? std::optional{FieldInput{.kind = FieldInput::Kind::Signed, .integer = *kInteger}}
                                    : std::nullopt;
    }
    if (kind == "unsigned") {
        const auto kWhole = integerOf<std::uint64_t>(&held);
        return kWhole.has_value() ? std::optional{FieldInput{.kind = FieldInput::Kind::Unsigned, .whole = *kWhole}}
                                  : std::nullopt;
    }
    if (kind == "real" && held.kind() == Value::Kind::Number && held.real().has_value() &&
        std::isfinite(*held.real())) {
        return FieldInput{.kind = FieldInput::Kind::Real, .real = *held.real()};
    }
    if (kind == "truth" && held.kind() == Value::Kind::Bool) {
        return FieldInput{.kind = FieldInput::Kind::Truth, .truth = *held.truth()};
    }
    return std::nullopt;
}

/// Where the operation `value` names stands among the declarations, its
/// inputs exactly the declaration's.
result::Result<std::size_t> declaredOf(const Value& value) {
    const std::string* name = textOf(value.find("operation"));
    if (value.kind() != Value::Kind::Object || name == nullptr) {
        return malformed("an operation is an object naming its operation");
    }
    const auto kDeclared = std::ranges::find(declarations(), *name, &OperationDeclaration::name);
    if (kDeclared == declarations().end()) {
        return result::fail(result::ErrorClass::Unsupported,
                            kAuthoringDomain,
                            code(AuthoringError::UnsupportedOperation),
                            "this surface generation has no such operation");
    }
    // Exactly the declared inputs, an optional one absent or present.
    for (const std::string& member : value.names()) {
        if (member != "operation" && !std::ranges::contains(kDeclared->inputs, member, &InputDeclaration::name)) {
            return malformed("an operation holds only its declared inputs");
        }
    }
    for (const InputDeclaration& input : kDeclared->inputs) {
        if (input.type != InputType::OptionalPlace && value.find(input.name) == nullptr) {
            return malformed("an operation holds every input its declaration lists");
        }
    }
    return static_cast<std::size_t>(kDeclared - declarations().begin());
}

constexpr std::size_t kChanging = std::variant_size_v<Operation>;

/// The query `value` names.
result::Result<Query> queryOf(const Value& value) {
    RAWFRAME_TRY_ASSIGN(const std::size_t kIndex, declaredOf(value));
    if (kIndex < kChanging) {
        return malformed("a query document reads the scene; a request changes it");
    }
    if (kIndex == kChanging) {
        return Query{ListEntities{}};
    }
    const auto kEntity = idOf(value.find("entity"));
    if (!kEntity.has_value()) {
        return malformed("an operation's inputs are of their declared types");
    }
    return Query{ReadEntity{.entity = *kEntity}};
}

/// The operation `value` names.
result::Result<Operation> operationOf(const Value& value) {
    RAWFRAME_TRY_ASSIGN(const std::size_t kIndex, declaredOf(value));
    if (kIndex >= kChanging) {
        return malformed("a request changes the scene; a query document reads it");
    }
    const auto kEntity = idOf(value.find("entity"));
    const auto kComponent = idOf(value.find("component"));
    const std::string* text = textOf(value.find("name"));
    const std::string* field = textOf(value.find("field"));
    const auto kBad = [] {
        return malformed("an operation's inputs are of their declared types");
    };
    if (kIndex == 8) {
        if (!kComponent.has_value()) {
            return kBad();
        }
        return Operation{RemarkComponent{.component = {*kComponent}}};
    }
    if (!kEntity.has_value()) {
        return kBad();
    }
    switch (kIndex) {
    case 0: {
        const Value* place = value.find("place");
        const std::optional<std::size_t> kPlace = place != nullptr ? placeOf(place) : std::nullopt;
        if (text == nullptr || (place != nullptr && !kPlace.has_value())) {
            return kBad();
        }
        return Operation{CreateEntity{.entity = *kEntity, .name = *text, .place = kPlace}};
    }
    case 1:
        return Operation{DestroyEntity{.entity = *kEntity}};
    case 2:
        if (text == nullptr) {
            return kBad();
        }
        return Operation{RenameEntity{.entity = *kEntity, .name = *text}};
    case 3: {
        const std::optional<std::size_t> kPlace = placeOf(value.find("place"));
        if (!kPlace.has_value()) {
            return kBad();
        }
        return Operation{MoveEntity{.entity = *kEntity, .place = *kPlace}};
    }
    case 4:
    case 5:
        if (!kComponent.has_value()) {
            return kBad();
        }
        if (kIndex == 4) {
            return Operation{AddComponent{.entity = *kEntity, .component = {*kComponent}}};
        }
        return Operation{RemoveComponent{.entity = *kEntity, .component = {*kComponent}}};
    case 6: {
        const std::optional<FieldInput> kValue = fieldInputOf(*value.find("value"));
        if (!kComponent.has_value() || field == nullptr || !kValue.has_value()) {
            return kBad();
        }
        return Operation{SetField{.entity = *kEntity, .component = {*kComponent}, .field = *field, .value = *kValue}};
    }
    case 7: {
        const Value& target = *value.find("target");
        const std::optional<base::Bits128> kTarget = target.isNull() ? std::nullopt : idOf(&target);
        if (!kComponent.has_value() || field == nullptr || (!target.isNull() && !kTarget.has_value())) {
            return kBad();
        }
        return Operation{
            SetReference{.entity = *kEntity, .component = {*kComponent}, .field = *field, .target = kTarget}};
    }
    case 9:
        if (!kComponent.has_value() || field == nullptr) {
            return kBad();
        }
        return Operation{RevertField{.entity = *kEntity, .component = {*kComponent}, .field = *field}};
    case 10:
        if (!kComponent.has_value()) {
            return kBad();
        }
        return Operation{RevertComponent{.entity = *kEntity, .component = {*kComponent}}};
    case 11:
        return Operation{RestoreEntity{.entity = *kEntity}};
    default:
        return kBad();
    }
}

constexpr std::array<std::string_view, 5> kFieldKindNames = {"signed", "unsigned", "real", "truth", "reference"};
constexpr std::array<std::string_view, 3> kPatchNames = {"set", "add", "remove"};

std::string idText(base::Bits128 id) {
    const auto kText = schema::formatStableIdText(id);
    return std::string{kText.data(), kText.size()};
}

std::string hexText(base::Bits128 id) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(id, digits);
    return std::string{digits.data(), digits.size()};
}

std::string markText(std::uint64_t mark) {
    std::array<char, 16> digits{};
    const char* const kEnd = std::to_chars(digits.data(), digits.data() + digits.size(), mark, 16).ptr;
    std::string text(digits.size() - static_cast<std::size_t>(kEnd - digits.data()), '0');
    text.append(std::string_view{digits.data(), kEnd});
    return text;
}

Value singleMember(std::string_view name, Value value) {
    Value made = Value::object();
    made.add(std::string{name}, std::move(value));
    return made;
}

/// A field's value in the form a request sets it with, or as the scene
/// records it when the catalog cannot type it.
Value readingValue(const FieldReading& field) {
    const scene::FieldValue& value = field.value;
    if (field.kind.has_value() && fits(value, *field.kind)) {
        switch (*field.kind) {
        case FieldKind::Signed:
            return singleMember("signed", Value::string(value.number));
        case FieldKind::Unsigned:
            return singleMember("unsigned", Value::string(value.number));
        case FieldKind::Real: {
            auto parsed = document::parse(value.number);
            if (parsed.has_value() && parsed->kind() == Value::Kind::Number) {
                return singleMember("real", std::move(*parsed));
            }
            break;
        }
        case FieldKind::Truth:
            return singleMember("truth", Value::boolean(value.kind == scene::FieldValue::Kind::True));
        case FieldKind::Reference:
            return singleMember("entity", Value::string(idText(value.entity)));
        }
    }
    switch (value.kind) {
    case scene::FieldValue::Kind::Number:
        return singleMember("recorded", singleMember("number", Value::string(value.number)));
    case scene::FieldValue::Kind::True:
    case scene::FieldValue::Kind::False:
        return singleMember("recorded", Value::boolean(value.kind == scene::FieldValue::Kind::True));
    case scene::FieldValue::Kind::Entity:
        return singleMember("recorded", singleMember("entity", Value::string(idText(value.entity))));
    }
    return {};
}

Value entryValue(const EntityEntry& entry) {
    Value made = Value::object();
    made.add("id", Value::string(idText(entry.id)));
    if (entry.place.has_value()) {
        made.add("name", Value::string(entry.name));
        made.add("place", Value::integer(static_cast<std::int64_t>(*entry.place)));
    }
    if (entry.brought.has_value()) {
        Value brought = Value::object();
        brought.add("instance", Value::integer(static_cast<std::int64_t>(entry.brought->instance)));
        brought.add("scene", Value::string(hexText(entry.brought->scene)));
        brought.add("source", Value::string(idText(entry.brought->source)));
        made.add("brought", std::move(brought));
        made.add("removed", Value::boolean(entry.removed));
    }
    return made;
}

} // namespace

result::Result<std::vector<Query>> readQueries(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return malformed("a query document is strict JSON");
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const Value* queries = parsed->find("queries");
    if (parsed->kind() != Value::Kind::Object || parsed->names().size() != 3 || textOf(kind) == nullptr ||
        *kind->text() != "authoring.query" || version == nullptr || version->integer() != 1 || queries == nullptr ||
        queries->kind() != Value::Kind::Array) {
        return malformed("a query document is authoring.query, format 1, and its queries");
    }
    std::vector<Query> made;
    for (std::size_t at = 0; at < queries->items().size(); ++at) {
        auto query = queryOf(queries->items()[at]);
        if (!query.has_value()) {
            return std::unexpected<result::Error>{std::move(query).error().withContext("index", std::to_string(at))};
        }
        made.push_back(*query);
    }
    return made;
}

document::Value answerValue(const Answer& answer) {
    if (const auto* list = std::get_if<EntityList>(&answer)) {
        Value entities = Value::array();
        for (const EntityEntry& entry : list->entities) {
            entities.push(entryValue(entry));
        }
        return singleMember("entities", std::move(entities));
    }
    const EntityReading& reading = std::get<EntityReading>(answer);
    Value components = Value::array();
    for (const ComponentReading& component : reading.components) {
        Value fields = Value::array();
        for (const FieldReading& field : component.fields) {
            Value made = Value::object();
            made.add("name", Value::string(field.name));
            made.add("value", readingValue(field));
            fields.push(std::move(made));
        }
        Value made = Value::object();
        made.add("component",
                 component.component.has_value() ? Value::string(idText(component.component->value)) : Value{});
        made.add("name", Value::string(component.name));
        if (component.patch.has_value()) {
            made.add("patch", Value::string(std::string{kPatchNames[static_cast<std::size_t>(*component.patch)]}));
        }
        made.add("fields", std::move(fields));
        components.push(std::move(made));
    }
    Value made = Value::object();
    made.add("entity", entryValue(reading.entity));
    made.add("components", std::move(components));
    return made;
}

result::Result<Request> readRequest(std::string_view text) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return malformed("a request is strict JSON");
    }
    const Value* kind = parsed->find("kind");
    const Value* version = parsed->find("formatVersion");
    const Value* batch = parsed->find("batch");
    const Value* operations = parsed->find("operations");
    const Value* expects = parsed->find("expects");
    const std::size_t kMembers = expects != nullptr ? 5 : 4;
    if (parsed->kind() != Value::Kind::Object || parsed->names().size() != kMembers || textOf(kind) == nullptr ||
        *kind->text() != "authoring.request" || version == nullptr || version->integer() != 1 ||
        textOf(batch) == nullptr || operations == nullptr || operations->kind() != Value::Kind::Array ||
        (expects != nullptr && textOf(expects) == nullptr)) {
        return malformed("a request is authoring.request, format 1, a batch, operations, and what it expects");
    }
    Request request;
    const std::string& batchName = *batch->text();
    if (batchName == "atomic") {
        request.batch = Batch::Atomic;
    } else if (batchName == "continue_per_item") {
        request.batch = Batch::ContinuePerItem;
    } else if (batchName == "halt_remaining") {
        request.batch = Batch::HaltRemaining;
    } else {
        return malformed("a request's batch is atomic, continue_per_item, or halt_remaining");
    }
    if (expects != nullptr) {
        request.expects = *expects->text();
    }
    for (std::size_t at = 0; at < operations->items().size(); ++at) {
        auto operation = operationOf(operations->items()[at]);
        if (!operation.has_value()) {
            return std::unexpected<result::Error>{
                std::move(operation).error().withContext("index", std::to_string(at))};
        }
        request.operations.push_back(std::move(*operation));
    }
    return request;
}

std::string writeDiscovery(const ComponentCatalog* catalog) {
    Value operations = Value::array();
    for (const OperationDeclaration& each : declarations()) {
        Value inputs = Value::array();
        for (const InputDeclaration& input : each.inputs) {
            Value made = Value::object();
            made.add("name", Value::string(std::string{input.name}));
            made.add("type", Value::string(std::string{kInputTypeNames[static_cast<std::size_t>(input.type)]}));
            inputs.push(std::move(made));
        }
        Value made = Value::object();
        made.add("name", Value::string(std::string{each.name}));
        made.add("since", Value::integer(each.since));
        made.add("history", Value::string(std::string{kHistoryNames[static_cast<std::size_t>(each.history)]}));
        made.add("targets", Value::string(std::string{each.targets}));
        made.add("inputs", std::move(inputs));
        operations.push(std::move(made));
    }
    Value errors = Value::array();
    for (const std::string_view kName : kCodeNames) {
        errors.push(Value::string(std::string{kName}));
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("authoring.discovery"));
    made.add("surfaceGeneration", Value::integer(kSurfaceGeneration));
    made.add("operations", std::move(operations));
    made.add("errors", std::move(errors));
    if (catalog != nullptr) {
        Value components = Value::array();
        for (const ComponentSchema& component : catalog->components()) {
            Value fields = Value::array();
            for (const FieldSchema& field : component.fields) {
                Value each = Value::object();
                each.add("name", Value::string(field.name));
                each.add("kind", Value::string(std::string{kFieldKindNames[static_cast<std::size_t>(field.kind)]}));
                fields.push(std::move(each));
            }
            Value each = Value::object();
            each.add("id", Value::string(idText(component.id.value)));
            each.add("name", Value::string(component.name));
            each.add("mark", Value::string(markText(component.mark)));
            each.add("fields", std::move(fields));
            components.push(std::move(each));
        }
        made.add("components", std::move(components));
    }
    return document::write(made);
}

std::string_view codeName(const result::Error& error) noexcept {
    const std::uint32_t kCode = error.code().value;
    if (error.domain() != kAuthoringDomain || kCode == 0 || kCode > kCodeNames.size()) {
        return "internal";
    }
    return kCodeNames[kCode - 1];
}

document::Value errorRecord(const result::Error& error) {
    Value details = Value::object();
    for (const auto& field : error.context()) {
        if (details.find(field.key) == nullptr) {
            details.add(std::string{field.key}, Value::string(std::string{field.value}));
        }
    }
    Value made = Value::object();
    made.add("code", Value::string(std::string{codeName(error)}));
    made.add("class", Value::string(std::string{result::describe(error.errorClass())}));
    made.add("message", Value::string(std::string{error.description()}));
    made.add("details", std::move(details));
    return made;
}

} // namespace rawframe::authoring
