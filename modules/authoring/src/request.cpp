#include "rawframe/authoring/request.h"

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

/// The operation `value` names, its inputs exactly the declaration's.
result::Result<Operation> operationOf(const Value& value) {
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
    const auto kEntity = idOf(value.find("entity"));
    const auto kComponent = idOf(value.find("component"));
    const std::string* text = textOf(value.find("name"));
    const std::string* field = textOf(value.find("field"));
    const auto kBad = [] {
        return malformed("an operation's inputs are of their declared types");
    };
    const std::size_t kIndex = static_cast<std::size_t>(kDeclared - declarations().begin());
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

} // namespace

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

std::string writeDiscovery() {
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
