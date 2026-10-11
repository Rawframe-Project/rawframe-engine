#include "rawframe/authoring/authored_styles.h"

#include "rawframe/authoring/errors.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

constexpr std::array<std::string_view, 3> kDocumentFields = {"kind", "formatVersion", "styles"};

/// The sheet's objects' members in order, by what holds them: the root,
/// a class in `styles`, and a variant's look.
std::span<const std::string_view> orderOf(std::string_view holder) noexcept {
    if (holder.empty()) {
        return kDocumentFields;
    }
    if (holder == "styles") {
        return ui::kStyleFields;
    }
    return ui::kStyleLookFields;
}

result::Result<ui::StyleSheet> sheetOf(std::string_view text) {
    auto read = ui::readStyles(text);
    if (!read.has_value()) {
        // What the reader refused, and where, said to whoever asked.
        std::string why = "the style sheet does not read: " + std::string{read.error().description()};
        for (const result::ContextField& kField : read.error().context()) {
            if (kField.key == "path") {
                why += " at " + std::string{kField.value};
            }
        }
        return std::unexpected<result::Error>{std::move(read).error().mappedTo(
            result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::ValidationFailed), why)};
    }
    return read;
}

result::Status reads(std::string_view text) {
    RAWFRAME_TRY(sheetOf(text));
    return {};
}

std::string hexOf(std::uint64_t id) {
    std::array<char, 16> digits{};
    for (std::size_t at = 0; at < digits.size(); ++at) {
        digits[at] = "0123456789abcdef"[(id >> (60 - 4 * at)) & 0xFU];
    }
    return std::string{digits.data(), digits.size()};
}

/// The path to class `style`, and to its member or part under it.
JsonPath classPath(std::uint64_t style) {
    return {JsonStep{.member = "styles", .key = "styleId", .id = hexOf(style)}};
}
JsonPath under(std::uint64_t style, std::initializer_list<std::string_view> members) {
    JsonPath path = classPath(style);
    for (const std::string_view kMember : members) {
        path.push_back(JsonStep{.member = std::string{kMember}, .key = {}, .id = {}});
    }
    return path;
}

/// The delta `operation` makes of `staged`; refused where it cannot be
/// made.
result::Result<JsonDelta> deltaOf(const Value& staged, const Operation& operation) {
    const auto kHas = [&staged](std::uint64_t style) {
        return placeAt(staged, classPath(style)).has_value();
    };
    const auto kUnknown = [] {
        return refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the sheet has no such class");
    };
    const auto kSet = [&staged](JsonPath path, std::optional<Value> to) {
        std::optional<Value> before = slotAt(staged, path);
        return JsonDelta{.path = std::move(path), .place = 0, .before = std::move(before), .after = std::move(to)};
    };
    if (const auto* set = std::get_if<SetStylePart>(&operation)) {
        if (std::ranges::find(ui::kStyleVariantFields, set->variant) == ui::kStyleVariantFields.end() ||
            std::ranges::find(ui::kStyleLookFields, set->part) == ui::kStyleLookFields.end()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a part is one a look has, of one of a class's variants");
        }
        if (!kHas(set->style)) {
            return kUnknown();
        }
        return kSet(under(set->style, {set->variant, set->part}), set->value);
    }
    if (const auto* create = std::get_if<CreateStyleClass>(&operation)) {
        const std::size_t kCount = staged.find("styles")->items().size();
        if (create->style == 0 || kHas(create->style)) {
            return refuse(result::ErrorClass::AlreadyExists,
                          AuthoringError::Conflict,
                          "a new class's identity is fresh and not nought");
        }
        if (create->place.value_or(kCount) > kCount) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a new class's place is among the sheet's classes or after them");
        }
        Value record = Value::object();
        record.add("styleId", Value::string(hexOf(create->style)));
        record.add("name", Value::string(create->name));
        return JsonDelta{.path = classPath(create->style),
                         .place = create->place.value_or(kCount),
                         .before = std::nullopt,
                         .after = std::move(record)};
    }
    if (const auto* remove = std::get_if<RemoveStyleClass>(&operation)) {
        const JsonPath kPath = classPath(remove->style);
        const std::optional<std::size_t> kPlace = placeAt(staged, kPath);
        if (!kPlace.has_value()) {
            return kUnknown();
        }
        return JsonDelta{.path = kPath, .place = *kPlace, .before = slotAt(staged, kPath), .after = std::nullopt};
    }
    if (const auto* rename = std::get_if<RenameStyleClass>(&operation)) {
        if (!kHas(rename->style)) {
            return kUnknown();
        }
        return kSet(under(rename->style, {"name"}), Value::string(rename->name));
    }
    if (const auto* timed = std::get_if<SetStyleTransitions>(&operation)) {
        if (!kHas(timed->style)) {
            return kUnknown();
        }
        return kSet(under(timed->style, {"transitions"}), timed->value);
    }
    return refuse(result::ErrorClass::InvalidArgument,
                  AuthoringError::ValidationFailed,
                  "the operation changes a scene, not a style sheet");
}

} // namespace

result::Result<std::unique_ptr<AuthoredStyles>>
AuthoredStyles::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    RAWFRAME_TRY_ASSIGN(ui::StyleSheet sheet, sheetOf(text));
    RAWFRAME_TRY_ASSIGN(JsonDocument opened, JsonDocument::open(text, orderOf, reads, limits));
    return std::unique_ptr<AuthoredStyles>{new AuthoredStyles(document, std::move(opened), std::move(sheet))};
}

std::optional<Value> AuthoredStyles::part(std::uint64_t style, std::string_view variant, std::string_view part) const {
    return slotAt(document_.root(), under(style, {variant, part}));
}

result::Result<Committed> AuthoredStyles::change(std::uint64_t generation, std::span<const Operation> operations) {
    RAWFRAME_TRY_ASSIGN(JsonDocument::Staged staged, document_.stage(generation));
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    for (std::size_t at = 0; at < operations.size(); ++at) {
        auto delta = deltaOf(staged.root, operations[at]);
        if (!delta.has_value()) {
            return std::unexpected<result::Error>{std::move(delta).error().withContext("index", std::to_string(at))};
        }
        auto applied = document_.apply(staged, std::move(*delta));
        if (!applied.has_value()) {
            return std::unexpected<result::Error>{std::move(applied).error().withContext("index", std::to_string(at))};
        }
    }
    return reread(document_.keep(std::move(staged)));
}

result::Result<Committed> AuthoredStyles::reread(result::Result<Committed> done) {
    if (done.has_value() && done->deltas > 0) {
        // What the document's reader has just read reads again.
        sheet_ = *sheetOf(document_.text());
    }
    return done;
}

result::Result<Committed> AuthoredStyles::undo(std::uint64_t generation) {
    return reread(document_.undo(generation));
}

result::Result<Committed> AuthoredStyles::redo(std::uint64_t generation) {
    return reread(document_.redo(generation));
}

std::string summaryOf(std::span<const JsonDelta> journal) {
    if (journal.empty()) {
        return "no change";
    }
    const JsonDelta& kFirst = journal.front();
    const auto kText = [](const std::optional<Value>& value, std::string_view member) {
        const Value* held = value.has_value() ? (member.empty() ? &*value : value->find(member)) : nullptr;
        return held != nullptr && held->text() != nullptr ? *held->text() : std::string{};
    };
    std::string made;
    if (kFirst.path.size() == 1) {
        made = kFirst.after.has_value() ? "create class " + kText(kFirst.after, "name")
                                        : "delete class " + kText(kFirst.before, "name");
    } else if (kFirst.path.size() == 3) {
        made = (kFirst.after.has_value() ? "set " : "unset ") + kFirst.path[1].member + "." + kFirst.path[2].member;
    } else if (kFirst.path[1].member == "name") {
        made = "rename class to " + kText(kFirst.after, "");
    } else {
        made = kFirst.after.has_value() ? "set transitions" : "unset transitions";
    }
    if (journal.size() > 1) {
        made += " and " + std::to_string(journal.size() - 1) + " more";
    }
    return made;
}

result::Result<Value> answer(const AuthoredStyles& styles, const Query& query) {
    if (!std::holds_alternative<ReadSheet>(query)) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     "the query reads a scene, not a style sheet")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value made = Value::object();
    made.add("sheet", styles.documentValue());
    return made;
}

} // namespace rawframe::authoring
