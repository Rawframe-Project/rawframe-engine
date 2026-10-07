#include "records.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <random>
#include <span>
#include <utility>

namespace rawframe::studio {

std::string shown(const Value& value) {
    if (value.kind() == Value::Kind::Object && value.names().size() == 1) {
        const Value& kOnly = *value.find(value.names().front());
        if (kOnly.kind() == Value::Kind::String) {
            return *kOnly.text();
        }
        return document::writeCompact(kOnly);
    }
    return document::writeCompact(value);
}

std::optional<Value> typedValue(std::string_view kind, std::string_view text) {
    if (kind == "real") {
        const auto kParsed = document::parse(text);
        if (!kParsed.has_value() || kParsed->kind() != Value::Kind::Number) {
            return std::nullopt;
        }
        Value made = Value::object();
        made.add("real", *kParsed);
        return made;
    }
    // A whole number is all its digits, signed or not, in its range: what
    // is not is no number, so an asset's name can be tried (D455).
    if (kind == "signed" || kind == "unsigned") {
        bool whole = false;
        if (kind == "signed") {
            std::int64_t value = 0;
            const auto [kStop, kError] = std::from_chars(text.data(), text.data() + text.size(), value);
            whole = kError == std::errc{} && kStop == text.data() + text.size();
        } else {
            std::uint64_t value = 0;
            const auto [kStop, kError] = std::from_chars(text.data(), text.data() + text.size(), value);
            whole = kError == std::errc{} && kStop == text.data() + text.size();
        }
        if (!whole) {
            return std::nullopt;
        }
    }
    if (kind == "signed" || kind == "unsigned" || kind == "case") {
        Value made = Value::object();
        made.add(std::string{kind}, Value::string(std::string{text}));
        return made;
    }
    if (kind == "truth" && (text == "true" || text == "false")) {
        Value made = Value::object();
        made.add("truth", Value::boolean(text == "true"));
        return made;
    }
    return std::nullopt;
}

std::optional<Value> firstAnswer(std::string_view reply) {
    auto parsed = document::parse(reply);
    const Value* answer = parsed.has_value() ? parsed->find("answer") : nullptr;
    const Value* answers = answer != nullptr ? answer->find("answers") : nullptr;
    if (answers == nullptr || answers->kind() != Value::Kind::Array || answers->items().empty()) {
        return std::nullopt;
    }
    const Value* first = answers->items().front().find("answer");
    return first != nullptr ? std::optional<Value>{*first} : std::nullopt;
}

Outcome outcomeOf(std::string_view reply) {
    Outcome outcome;
    const auto kParsed = document::parse(reply);
    if (!kParsed.has_value()) {
        outcome.message = "no answer";
        return outcome;
    }
    const Value* answer = kParsed->find("answer");
    const Value* results = answer != nullptr ? answer->find("results") : nullptr;
    const bool kSlot = results != nullptr && results->kind() == Value::Kind::Array && !results->items().empty();
    const Value* slot = kSlot ? &results->items()[0] : nullptr;
    outcome.done = slot != nullptr && slot->find("deltas") != nullptr;
    const Value* error = slot != nullptr ? slot->find("error") : kParsed->find("error");
    const Value* message = error != nullptr ? error->find("message") : nullptr;
    outcome.message = message != nullptr && message->text() != nullptr ? *message->text() : "refused";
    for (const auto& [kName, kInto] :
         {std::pair{"undoable", &outcome.undoable}, std::pair{"redoable", &outcome.redoable}}) {
        const Value* count = answer != nullptr ? answer->find(kName) : nullptr;
        *kInto = count != nullptr ? count->integer().value_or(0) : 0;
    }
    if (const Value* view = answer != nullptr ? answer->find("view") : nullptr; view != nullptr) {
        outcome.view = *view;
    }
    return outcome;
}

Answered answeredOf(std::string_view reply) {
    Answered answered;
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (answer == nullptr) {
        const Value* error = kParsed.has_value() ? kParsed->find("error") : nullptr;
        const Value* message = error != nullptr ? error->find("message") : nullptr;
        answered.message = message != nullptr && message->text() != nullptr ? *message->text() : "no answer";
        return answered;
    }
    answered.done = true;
    if (const Value* view = answer->find("view"); view != nullptr) {
        answered.view = *view;
    }
    const Value* previewing = answer->find("previewing");
    answered.previewing = previewing != nullptr && previewing->truth().value_or(false);
    if (const Value* resource = answer->find("resource"); resource != nullptr && resource->text() != nullptr) {
        answered.resource = *resource->text();
    }
    return answered;
}

namespace {

/// A component name's part after its last dot.
std::string_view lastPart(std::string_view name) {
    const std::size_t kDot = name.rfind('.');
    return kDot == std::string_view::npos ? name : name.substr(kDot + 1);
}

Value recordOf(std::string_view kind, std::int64_t id, std::string_view scene) {
    Value record = Value::object();
    record.add("kind", Value::string(std::string{kind}));
    record.add("id", Value::integer(id));
    record.add("scene", Value::string(std::string{scene}));
    return record;
}

} // namespace

Value readRecord(std::int64_t id, std::string_view scene, std::string_view operation, std::string_view entity) {
    Value query = Value::object();
    query.add("operation", Value::string(std::string{operation}));
    if (!entity.empty()) {
        query.add("entity", Value::string(std::string{entity}));
    }
    Value queries = Value::array();
    queries.push(std::move(query));
    Value document = Value::object();
    document.add("formatVersion", Value::integer(1));
    document.add("kind", Value::string("authoring.query"));
    document.add("queries", std::move(queries));
    Value record = recordOf("authoring.read", id, scene);
    record.add("queries", std::move(document));
    return record;
}

Value applyRecord(std::int64_t id, std::string_view scene, Value operation) {
    std::vector<Value> operations;
    operations.push_back(std::move(operation));
    return applyRecord(id, scene, std::move(operations));
}

Value applyRecord(std::int64_t id, std::string_view scene, std::vector<Value> given) {
    Value operations = Value::array();
    for (Value& operation : given) {
        operations.push(std::move(operation));
    }
    Value request = Value::object();
    request.add("formatVersion", Value::integer(1));
    request.add("kind", Value::string("authoring.request"));
    request.add("batch", Value::string("atomic"));
    request.add("operations", std::move(operations));
    Value record = recordOf("authoring.apply", id, scene);
    record.add("request", std::move(request));
    return record;
}

Value selectRecord(std::int64_t id, std::string_view scene, std::string_view entity) {
    Value entities = Value::array();
    entities.push(Value::string(std::string{entity}));
    Value record = recordOf("authoring.select", id, scene);
    record.add("entities", std::move(entities));
    return record;
}

Value stepRecord(std::int64_t id, std::string_view kind, std::string_view scene) {
    return recordOf(kind, id, scene);
}

Value viewRecord(std::int64_t id, std::string_view scene, const Value& view) {
    Value record = recordOf("authoring.view", id, scene);
    record.add("view", view);
    return record;
}

Value historyRecord(std::int64_t id, std::string_view scene) {
    return recordOf("authoring.history", id, scene);
}

std::vector<HistoryEntry> historyOf(std::string_view reply) {
    std::vector<HistoryEntry> made;
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* entries = answer != nullptr ? answer->find("entries") : nullptr;
    if (entries == nullptr || entries->kind() != Value::Kind::Array) {
        return made;
    }
    for (const Value& each : entries->items()) {
        const Value* summary = each.find("summary");
        const Value* applied = each.find("applied");
        made.push_back(
            HistoryEntry{.summary = summary != nullptr && summary->text() != nullptr ? *summary->text() : "?",
                         .applied = applied != nullptr && applied->truth().value_or(false)});
    }
    return made;
}

Value assetsRecord(std::int64_t id) {
    Value record = Value::object();
    record.add("kind", Value::string("authoring.assets"));
    record.add("id", Value::integer(id));
    return record;
}

std::vector<Asset> assetsOf(std::string_view reply) {
    std::vector<Asset> made;
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* assets = answer != nullptr ? answer->find("assets") : nullptr;
    if (assets == nullptr || assets->kind() != Value::Kind::Array) {
        return made;
    }
    for (const Value& each : assets->items()) {
        const Value* kind = each.find("kind");
        const Value* id = each.find("id");
        const Value* name = each.find("name");
        if (kind == nullptr || kind->text() == nullptr || id == nullptr || id->text() == nullptr ||
            id->text()->size() != 16 || name == nullptr || name->text() == nullptr) {
            continue;
        }
        std::uint64_t value = 0;
        const std::string& digits = *id->text();
        if (std::from_chars(digits.data(), digits.data() + digits.size(), value, 16).ptr !=
            digits.data() + digits.size()) {
            continue;
        }
        made.push_back(Asset{.kind = *kind->text(), .id = value, .name = *name->text()});
    }
    return made;
}

const Asset* assetHeld(std::span<const Asset> assets, std::string_view value) {
    std::uint64_t number = 0;
    const auto [kStop, kError] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (kError != std::errc{} || kStop != value.data() + value.size() || number == 0) {
        return nullptr;
    }
    const auto kFound = std::ranges::find(assets, number, &Asset::id);
    return kFound != assets.end() ? &*kFound : nullptr;
}

std::string assetShown(const Asset& asset) {
    return asset.name + " (" + asset.kind + ")";
}

std::optional<Asset> assetNamed(std::span<const Asset> assets, std::string_view text, std::string& why) {
    if (text.empty()) {
        why = "name an asset";
        return std::nullopt;
    }
    const auto kFile = [](std::string_view name) {
        const std::size_t kSlash = name.rfind('/');
        return kSlash == std::string_view::npos ? name : name.substr(kSlash + 1);
    };
    const std::array<std::function<bool(const Asset&)>, 4> kRules = {[&text](const Asset& asset) {
                                                                         return assetShown(asset) == text;
                                                                     },
                                                                     [&text](const Asset& asset) {
                                                                         return asset.name == text;
                                                                     },
                                                                     [&text, &kFile](const Asset& asset) {
                                                                         return kFile(asset.name) == text;
                                                                     },
                                                                     [&text](const Asset& asset) {
                                                                         return asset.name.starts_with(text);
                                                                     }};
    for (const auto& kRule : kRules) {
        std::vector<const Asset*> met;
        for (const Asset& asset : assets) {
            if (kRule(asset)) {
                met.push_back(&asset);
            }
        }
        if (met.size() == 1) {
            return *met.front();
        }
        if (met.size() > 1) {
            why = std::to_string(met.size()) + " assets match " + std::string{text};
            return std::nullopt;
        }
    }
    why = "no asset matches " + std::string{text};
    return std::nullopt;
}

Value pickRecord(std::int64_t id, std::string_view scene) {
    return recordOf("authoring.pick", id, scene);
}

std::optional<std::string> pickedIn(std::string_view reply, std::string_view scene) {
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* picked = answer != nullptr ? answer->find("scene") : nullptr;
    const Value* source = answer != nullptr ? answer->find("source") : nullptr;
    if (picked == nullptr || picked->text() == nullptr || *picked->text() != scene || source == nullptr ||
        source->text() == nullptr) {
        return std::nullopt;
    }
    return *source->text();
}

std::optional<Moved> movedIn(std::string_view reply, std::string_view scene) {
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* moved = answer != nullptr ? answer->find("moved") : nullptr;
    const Value* where = moved != nullptr ? moved->find("scene") : nullptr;
    const Value* source = moved != nullptr ? moved->find("source") : nullptr;
    const Value* by = moved != nullptr ? moved->find("by") : nullptr;
    if (where == nullptr || where->text() == nullptr || *where->text() != scene || source == nullptr ||
        source->text() == nullptr || by == nullptr || by->kind() != Value::Kind::Array || by->items().size() != 3) {
        return std::nullopt;
    }
    const auto kPoint = [](const Value* point) -> std::optional<std::array<double, 3>> {
        if (point == nullptr || point->kind() != Value::Kind::Array || point->items().size() != 3) {
            return std::nullopt;
        }
        std::array<double, 3> made{};
        for (std::size_t at = 0; at < 3; ++at) {
            const std::optional<double> kEach = point->items()[at].real();
            if (!kEach.has_value() || !std::isfinite(*kEach)) {
                return std::nullopt;
            }
            made[at] = *kEach;
        }
        return made;
    };
    const auto kBy = kPoint(by);
    const Value* how = moved->find("how");
    const std::string_view kHow = how != nullptr && how->text() != nullptr ? std::string_view{*how->text()} : "move";
    if (!kBy.has_value()) {
        return std::nullopt;
    }
    Moved made{.source = *source->text(), .x = (*kBy)[0], .y = (*kBy)[1], .z = (*kBy)[2]};
    if (kHow == "height") {
        made.how = Moved::How::Height;
    } else if (kHow == "turn") {
        const auto kFrom = kPoint(moved->find("from"));
        const auto kTo = kPoint(moved->find("to"));
        if (!kFrom.has_value() || !kTo.has_value()) {
            return std::nullopt;
        }
        made.how = Moved::How::Turn;
        made.from = *kFrom;
        made.to = *kTo;
    }
    return made;
}

Value createSceneRecord(std::int64_t id, std::string_view scene) {
    return recordOf("authoring.create_scene", id, scene);
}

Value previewRecord(std::int64_t id, std::string_view scene, const Preview* preview) {
    Value record = recordOf("authoring.preview", id, scene);
    if (preview == nullptr) {
        record.add("preview", Value{});
        return record;
    }
    Value made = Value::object();
    made.add("endpoint", Value::string(preview->endpoint));
    made.add("pinFile", Value::string(preview->pinFile));
    made.add("tokenFile", Value::string(preview->tokenFile));
    if (!preview->serverEndpoint.empty()) {
        Value server = Value::object();
        server.add("endpoint", Value::string(preview->serverEndpoint));
        server.add("pinFile", Value::string(preview->serverPinFile));
        made.add("server", std::move(server));
    }
    record.add("preview", std::move(made));
    return record;
}

namespace {

/// The numbers `text` holds apart by spaces or commas, if every part is one.
std::optional<std::vector<double>> numbersOf(std::string_view text) {
    std::vector<double> numbers;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = text.find_first_of(" ,", at);
        const std::string_view kPart = text.substr(at, kEnd == std::string_view::npos ? text.size() - at : kEnd - at);
        if (!kPart.empty()) {
            const auto kParsed = document::parse(kPart);
            if (!kParsed.has_value() || !kParsed->real().has_value()) {
                return std::nullopt;
            }
            numbers.push_back(*kParsed->real());
        }
        if (kEnd == std::string_view::npos) {
            break;
        }
        at = kEnd + 1;
    }
    return numbers;
}

Value point(double x, double y, double z) {
    Value made = Value::array();
    made.push(Value::real(x));
    made.push(Value::real(y));
    made.push(Value::real(z));
    return made;
}

} // namespace

std::optional<Value> viewWith(const std::optional<Value>& current, std::string_view part, std::string_view text) {
    const auto kNumbers = numbersOf(text);
    const std::size_t kWanted = part == "fieldOfView" ? 1 : 3;
    if (!kNumbers.has_value() || kNumbers->size() != kWanted ||
        (part != "eye" && part != "target" && part != "fieldOfView")) {
        return std::nullopt;
    }
    Value made = Value::object();
    for (const std::string_view kName : {"eye", "target", "fieldOfView"}) {
        const Value* held = current.has_value() && !current->isNull() ? current->find(kName) : nullptr;
        if (kName == part) {
            made.add(std::string{kName},
                     kWanted == 1 ? Value::real((*kNumbers)[0])
                                  : point((*kNumbers)[0], (*kNumbers)[1], (*kNumbers)[2]));
        } else if (held != nullptr) {
            made.add(std::string{kName}, *held);
        } else if (kName == "eye") {
            made.add("eye", point(0, 10, 10));
        } else if (kName == "target") {
            made.add("target", point(0, 0, 0));
        } else {
            made.add("fieldOfView", Value::real(60));
        }
    }
    return made;
}

std::string viewText(const std::optional<Value>& view, std::string_view part) {
    const Value* held = view.has_value() && !view->isNull() ? view->find(part) : nullptr;
    if (held == nullptr) {
        return {};
    }
    if (held->kind() != Value::Kind::Array) {
        return document::writeCompact(*held);
    }
    std::string text;
    for (const Value& each : held->items()) {
        text += (text.empty() ? "" : " ") + document::writeCompact(each);
    }
    return text;
}

bool Catalog::offers(std::string_view operation) const {
    return std::ranges::find(operations, operation) != operations.end();
}

const Catalog::Component* Catalog::component(std::string_view id) const {
    const auto kFound = std::ranges::find(components, id, &Component::id);
    return kFound != components.end() ? &*kFound : nullptr;
}

Catalog catalogOf(std::string_view reply) {
    Catalog catalog;
    const auto kParsed = document::parse(reply);
    const Value* answer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* operations = answer != nullptr ? answer->find("operations") : nullptr;
    const Value* components = answer != nullptr ? answer->find("components") : nullptr;
    for (const Value& each : operations != nullptr && operations->kind() == Value::Kind::Array
                                 ? operations->items()
                                 : std::span<const Value>{}) {
        if (const Value* name = each.find("name"); name != nullptr && name->text() != nullptr) {
            catalog.operations.push_back(*name->text());
        }
    }
    for (const Value& each : components != nullptr && components->kind() == Value::Kind::Array
                                 ? components->items()
                                 : std::span<const Value>{}) {
        const Value* id = each.find("id");
        const Value* name = each.find("name");
        if (id == nullptr || id->text() == nullptr || name == nullptr || name->text() == nullptr) {
            continue;
        }
        Catalog::Component made{.id = *id->text(), .name = *name->text(), .fields = {}};
        const Value* fields = each.find("fields");
        for (const Value& field :
             fields != nullptr && fields->kind() == Value::Kind::Array ? fields->items() : std::span<const Value>{}) {
            const Value* fieldName = field.find("name");
            const Value* kind = field.find("kind");
            if (fieldName != nullptr && fieldName->text() != nullptr) {
                made.fields.push_back(
                    {*fieldName->text(), kind != nullptr && kind->text() != nullptr ? *kind->text() : std::string{}});
            }
        }
        catalog.components.push_back(std::move(made));
    }
    return catalog;
}

std::optional<Catalog::Component> componentNamed(const Catalog& catalog, std::string_view text, std::string& why) {
    if (text.empty()) {
        why = "name a component";
        return std::nullopt;
    }
    // A whole name, then a last part, then a start: the first rule one
    // component alone meets decides.
    const std::array<bool (*)(std::string_view, std::string_view), 3> kRules = {
        [](std::string_view name, std::string_view typed) {
            return name == typed;
        },
        [](std::string_view name, std::string_view typed) {
            return lastPart(name) == typed;
        },
        [](std::string_view name, std::string_view typed) {
            return name.starts_with(typed) || lastPart(name).starts_with(typed);
        }};
    for (const auto kRule : kRules) {
        std::vector<const Catalog::Component*> met;
        for (const Catalog::Component& each : catalog.components) {
            if (kRule(each.name, text)) {
                met.push_back(&each);
            }
        }
        if (met.size() == 1) {
            return *met.front();
        }
        if (met.size() > 1) {
            why = std::to_string(met.size()) + " components match " + std::string{text};
            return std::nullopt;
        }
    }
    why = "no component matches " + std::string{text};
    return std::nullopt;
}

std::vector<FieldShown> fieldsShown(const Catalog::Component* type, const Value* fields) {
    std::vector<FieldShown> shownFields;
    if (type != nullptr) {
        for (const Catalog::Field& kField : type->fields) {
            shownFields.push_back({kField.name, kField.kind, std::nullopt});
        }
    }
    for (const Value& each :
         fields != nullptr && fields->kind() == Value::Kind::Array ? fields->items() : std::span<const Value>{}) {
        const Value* name = each.find("name");
        const Value* value = each.find("value");
        if (name == nullptr || name->text() == nullptr) {
            continue;
        }
        auto found = std::ranges::find(shownFields, *name->text(), &FieldShown::name);
        if (found == shownFields.end()) {
            shownFields.push_back({*name->text(), {}, std::nullopt});
            found = std::prev(shownFields.end());
        }
        found->text = value != nullptr ? shown(*value) : std::string{};
        if (value != nullptr && value->kind() == Value::Kind::Object && value->names().size() == 1) {
            found->kind = value->names().front();
        }
    }
    return shownFields;
}

std::optional<std::size_t> sceneNamed(std::span<const std::string> scenes, std::string_view text, std::string& why) {
    if (text.empty()) {
        why = "name a scene";
        return std::nullopt;
    }
    const auto kFile = [](std::string_view path) {
        const std::size_t kSlash = path.rfind('/');
        return kSlash == std::string_view::npos ? path : path.substr(kSlash + 1);
    };
    const auto kStem = [&kFile](std::string_view path) {
        const std::string_view kName = kFile(path);
        return kName.ends_with(".scene") ? kName.substr(0, kName.size() - 6) : kName;
    };
    const auto kWhole = [&](std::string_view path) {
        return path == text || kFile(path) == text || kStem(path) == text;
    };
    const auto kStart = [&](std::string_view path) {
        return path.starts_with(text) || kFile(path).starts_with(text);
    };
    for (const auto& kRule :
         {std::function<bool(std::string_view)>{kWhole}, std::function<bool(std::string_view)>{kStart}}) {
        std::vector<std::size_t> met;
        for (std::size_t each = 0; each < scenes.size(); ++each) {
            if (kRule(scenes[each])) {
                met.push_back(each);
            }
        }
        if (met.size() == 1) {
            return met.front();
        }
        if (met.size() > 1) {
            why = std::to_string(met.size()) + " scenes match " + std::string{text};
            return std::nullopt;
        }
    }
    why = "no scene matches " + std::string{text};
    return std::nullopt;
}

std::optional<std::string> entityNamed(std::span<const std::string> ids,
                                       std::span<const std::string> names,
                                       std::string_view text,
                                       std::string& why) {
    if (text.empty()) {
        why = "name an entity";
        return std::nullopt;
    }
    const std::size_t kCount = std::min(ids.size(), names.size());
    // A whole name, then an identity's start, then a name's start: the
    // first rule one entity alone meets decides.
    const std::array<bool (*)(std::string_view, std::string_view, std::string_view), 3> kRules = {
        [](std::string_view, std::string_view name, std::string_view typed) {
            return name == typed;
        },
        [](std::string_view id, std::string_view, std::string_view typed) {
            return typed.size() >= 4 && id.starts_with(typed);
        },
        [](std::string_view, std::string_view name, std::string_view typed) {
            return name.starts_with(typed);
        }};
    for (const auto kRule : kRules) {
        std::vector<std::size_t> met;
        for (std::size_t each = 0; each < kCount; ++each) {
            if (kRule(ids[each], names[each], text)) {
                met.push_back(each);
            }
        }
        if (met.size() == 1) {
            return ids[met.front()];
        }
        if (met.size() > 1) {
            why = std::to_string(met.size()) + " entities match " + std::string{text};
            return std::nullopt;
        }
    }
    why = "no entity matches " + std::string{text};
    return std::nullopt;
}

std::optional<Diagnostic> diagnosticOf(std::string_view reply) {
    const auto kParsed = document::parse(reply);
    const Value* error = kParsed.has_value() ? kParsed->find("error") : nullptr;
    const Value* details = error != nullptr ? error->find("details") : nullptr;
    const Value* text = details != nullptr ? details->find("diagnostic") : nullptr;
    if (text == nullptr || text->text() == nullptr) {
        return std::nullopt;
    }
    // `file:line:column: message`: the location ends at the first ": ".
    const std::string_view kWhole = *text->text();
    const std::size_t kEnd = kWhole.find(": ");
    const std::string_view kWhere = kWhole.substr(0, kEnd);
    const std::size_t kColumnAt = kWhere.rfind(':');
    const std::size_t kLineAt = kColumnAt == std::string_view::npos || kColumnAt == 0
                                    ? std::string_view::npos
                                    : kWhere.rfind(':', kColumnAt - 1);
    if (kEnd == std::string_view::npos || kLineAt == std::string_view::npos || kLineAt == 0) {
        return std::nullopt;
    }
    const auto kNumber = [](std::string_view digits) -> std::optional<std::int64_t> {
        std::int64_t value = 0;
        const auto [kStop, kError] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        return kError == std::errc{} && kStop == digits.data() + digits.size() && value > 0 ? std::optional{value}
                                                                                            : std::nullopt;
    };
    const auto kLine = kNumber(kWhere.substr(kLineAt + 1, kColumnAt - kLineAt - 1));
    const auto kColumn = kNumber(kWhere.substr(kColumnAt + 1));
    if (!kLine.has_value() || !kColumn.has_value()) {
        return std::nullopt;
    }
    return Diagnostic{.file = std::string{kWhere.substr(0, kLineAt)},
                      .line = *kLine,
                      .column = *kColumn,
                      .message = std::string{kWhole.substr(kEnd + 2)}};
}

std::vector<std::string>
editorCommand(std::string_view command, const std::string& file, std::int64_t line, std::int64_t column) {
    std::vector<std::string> made;
    std::size_t at = 0;
    while (at < command.size()) {
        const std::size_t kEnd = std::min(command.find(' ', at), command.size());
        std::string word{command.substr(at, kEnd - at)};
        at = kEnd + 1;
        if (word.empty()) {
            continue;
        }
        for (const auto& [kName, kValue] : {std::pair<std::string_view, std::string>{"{file}", file},
                                            {"{line}", std::to_string(line)},
                                            {"{column}", std::to_string(column)}}) {
            for (std::size_t found = word.find(kName); found != std::string::npos;
                 found = word.find(kName, found + kValue.size())) {
                word.replace(found, kName.size(), kValue);
            }
        }
        made.push_back(std::move(word));
    }
    return made;
}

std::optional<std::filesystem::path> programOnPath(const std::string& program, std::string_view path) {
    if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos) {
        return std::filesystem::path{program};
    }
#if defined(_WIN32)
    constexpr char kSeparator = ';';
    constexpr std::array<std::string_view, 3> kEndings = {"", ".exe", ".cmd"};
#else
    constexpr char kSeparator = ':';
    constexpr std::array<std::string_view, 1> kEndings = {""};
#endif
    std::size_t at = 0;
    while (at <= path.size()) {
        const std::size_t kEnd = std::min(path.find(kSeparator, at), path.size());
        const std::string_view kDirectory = path.substr(at, kEnd - at);
        at = kEnd + 1;
        if (kDirectory.empty()) {
            continue;
        }
        for (const std::string_view kEnding : kEndings) {
            std::error_code error;
            const std::filesystem::path kFound = std::filesystem::path{kDirectory} / (program + std::string{kEnding});
            if (std::filesystem::is_regular_file(kFound, error)) {
                return kFound;
            }
        }
    }
    return std::nullopt;
}

bool isBatch(const std::filesystem::path& program) {
    std::string ending = program.extension().string();
    std::ranges::transform(ending, ending.begin(), [](unsigned char letter) {
        return static_cast<char>(letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter);
    });
    return ending == ".cmd" || ending == ".bat";
}

bool batchSafe(std::string_view word) {
    constexpr std::string_view kMarks = " _-.:/\\+,=@#~{}[]'";
    return std::ranges::all_of(word, [&kMarks](char each) {
        const auto kByte = static_cast<unsigned char>(each);
        return (kByte >= '0' && kByte <= '9') || (kByte >= 'a' && kByte <= 'z') || (kByte >= 'A' && kByte <= 'Z') ||
               kMarks.find(each) != std::string_view::npos;
    });
}

std::string mintedIdentity() {
    std::random_device device;
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t each = 0; each < bytes.size(); each += 4) {
        const std::uint32_t kWord = device();
        for (std::size_t part = 0; part < 4; ++part) {
            bytes[each + part] = static_cast<std::uint8_t>(kWord >> (8U * part));
        }
    }
    // Version 4, variant 1 (RFC 9562).
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (std::size_t each = 0; each < bytes.size(); ++each) {
        if (each == 4 || each == 6 || each == 8 || each == 10) {
            text += '-';
        }
        text += kDigits[bytes[each] >> 4U];
        text += kDigits[bytes[each] & 0x0FU];
    }
    return text;
}

} // namespace rawframe::studio
