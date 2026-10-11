#include "rawframe/authoring/authored_styles.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/base/hex64.h"

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

result::Result<ui::StyleSheet> sheetOf(const std::string& text) {
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

/// Where class `style` is among the document's, if it is.
std::optional<std::size_t> placeOf(const Value& root, std::uint64_t style) {
    const Value* styles = root.find("styles");
    if (styles == nullptr) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at < styles->items().size(); ++at) {
        const Value* id = styles->items()[at].find("styleId");
        if (id != nullptr && id->text() != nullptr && base::parseHex64(*id->text()) == style) {
            return at;
        }
    }
    return std::nullopt;
}

/// `object` with its member `name` set to `value`, or gone for none, its
/// members in `order`.
Value withMember(const Value& object,
                 std::span<const std::string_view> order,
                 std::string_view name,
                 const std::optional<Value>& value) {
    Value made = Value::object();
    for (const std::string_view kField : order) {
        if (kField == name) {
            if (value.has_value()) {
                made.add(std::string{kField}, *value);
            }
        } else if (const Value* held = object.find(kField)) {
            made.add(std::string{kField}, *held);
        }
    }
    return made;
}

constexpr std::array<std::string_view, 3> kDocumentFields = {"kind", "formatVersion", "styles"};

bool same(const std::optional<Value>& left, const std::optional<Value>& right) {
    return left.has_value() == right.has_value() &&
           (!left.has_value() || document::write(*left) == document::write(*right));
}

/// The value of `part` of `variant` of class `style` in `root`.
std::optional<Value> partIn(const Value& root, std::uint64_t style, std::string_view variant, std::string_view part) {
    const std::optional<std::size_t> kPlace = placeOf(root, style);
    if (!kPlace.has_value()) {
        return std::nullopt;
    }
    const Value* look = root.find("styles")->items()[*kPlace].find(variant);
    const Value* held = look != nullptr ? look->find(part) : nullptr;
    return held != nullptr ? std::optional<Value>{*held} : std::nullopt;
}

/// The record of class `style` in `root`, if it has one.
std::optional<Value> classIn(const Value& root, std::uint64_t style) {
    const std::optional<std::size_t> kPlace = placeOf(root, style);
    return kPlace.has_value() ? std::optional<Value>{root.find("styles")->items()[*kPlace]} : std::nullopt;
}

/// The class's member a whole-member delta sets.
std::string_view memberOf(const StyleDelta& delta) {
    return delta.kind == StyleDelta::Kind::SetName ? "name" : "transitions";
}

/// What `delta`'s slot holds in `root`.
std::optional<Value> slotIn(const Value& root, const StyleDelta& delta) {
    switch (delta.kind) {
    case StyleDelta::Kind::SetPart:
        return partIn(root, delta.style, delta.variant, delta.part);
    case StyleDelta::Kind::CreateClass:
    case StyleDelta::Kind::DestroyClass:
        return classIn(root, delta.style);
    case StyleDelta::Kind::SetName:
    case StyleDelta::Kind::SetTransitions: {
        const std::optional<Value> kClass = classIn(root, delta.style);
        const Value* held = kClass.has_value() ? kClass->find(memberOf(delta)) : nullptr;
        return held != nullptr ? std::optional<Value>{*held} : std::nullopt;
    }
    }
    return std::nullopt;
}

/// `delta` applied to `root` forward or backward, where its slot holds what
/// it leaves from.
result::Status applyTo(Value& root, const StyleDelta& delta, bool forward) {
    const std::optional<Value>& kFrom = forward ? delta.before : delta.after;
    const std::optional<Value>& kTo = forward ? delta.after : delta.before;
    if (!same(slotIn(root, delta), kFrom)) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::DeltaMismatch,
                      "a class's slot does not hold what its change leaves from");
    }
    const Value& kClasses = *root.find("styles");
    const std::optional<std::size_t> kPlace = placeOf(root, delta.style);
    Value classes = Value::array();
    if (delta.kind == StyleDelta::Kind::CreateClass || delta.kind == StyleDelta::Kind::DestroyClass) {
        // A class comes or goes at its place.
        const std::size_t kAt = kPlace.value_or(delta.place);
        if (!kPlace.has_value() && kAt > kClasses.items().size()) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          AuthoringError::DeltaMismatch,
                          "a class's place is past the sheet's classes");
        }
        for (std::size_t each = 0; each <= kClasses.items().size(); ++each) {
            if (each == kAt && kTo.has_value()) {
                classes.push(*kTo);
            }
            if (each < kClasses.items().size() && !(kPlace.has_value() && each == *kPlace)) {
                classes.push(kClasses.items()[each]);
            }
        }
    } else {
        if (!kPlace.has_value()) {
            return refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the sheet has no such class");
        }
        const Value& kClass = kClasses.items()[*kPlace];
        Value changed;
        if (delta.kind == StyleDelta::Kind::SetName || delta.kind == StyleDelta::Kind::SetTransitions) {
            changed = withMember(kClass, ui::kStyleFields, memberOf(delta), kTo);
        } else {
            const Value* look = kClass.find(delta.variant);
            Value parts = withMember(look != nullptr ? *look : Value::object(), ui::kStyleLookFields, delta.part, kTo);
            // A look that sets nothing is no variant.
            const std::optional<Value> kLook =
                parts.names().empty() ? std::nullopt : std::optional<Value>{std::move(parts)};
            changed = withMember(kClass, ui::kStyleFields, delta.variant, kLook);
        }
        for (std::size_t each = 0; each < kClasses.items().size(); ++each) {
            classes.push(each == *kPlace ? changed : kClasses.items()[each]);
        }
    }
    root = withMember(root, kDocumentFields, "styles", classes);
    return {};
}

/// The delta `operation` makes of `staged`, none where it changes nothing;
/// refused where it cannot be made.
result::Result<std::optional<StyleDelta>> deltaOf(const Value& staged, const Operation& operation) {
    const auto kUnknown = [] {
        return refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the sheet has no such class");
    };
    std::optional<StyleDelta> delta;
    if (const auto* set = std::get_if<SetStylePart>(&operation)) {
        if (std::ranges::find(ui::kStyleVariantFields, set->variant) == ui::kStyleVariantFields.end() ||
            std::ranges::find(ui::kStyleLookFields, set->part) == ui::kStyleLookFields.end()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a part is one a look has, of one of a class's variants");
        }
        if (!placeOf(staged, set->style).has_value()) {
            return kUnknown();
        }
        delta = StyleDelta{.style = set->style,
                           .variant = set->variant,
                           .part = set->part,
                           .before = partIn(staged, set->style, set->variant, set->part),
                           .after = set->value};
    } else if (const auto* create = std::get_if<CreateStyleClass>(&operation)) {
        const std::size_t kCount = staged.find("styles")->items().size();
        if (create->style == 0 || placeOf(staged, create->style).has_value()) {
            return refuse(result::ErrorClass::AlreadyExists,
                          AuthoringError::Conflict,
                          "a new class's identity is fresh and not nought");
        }
        if (create->place.value_or(kCount) > kCount) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a new class's place is among the sheet's classes or after them");
        }
        std::array<char, 16> digits{};
        for (std::size_t at = 0; at < digits.size(); ++at) {
            digits[at] = "0123456789abcdef"[(create->style >> (60 - 4 * at)) & 0xFU];
        }
        Value record = Value::object();
        record.add("styleId", Value::string(std::string{digits.data(), digits.size()}));
        record.add("name", Value::string(create->name));
        delta = StyleDelta{.kind = StyleDelta::Kind::CreateClass,
                           .style = create->style,
                           .place = create->place.value_or(kCount),
                           .before = std::nullopt,
                           .after = std::move(record)};
    } else if (const auto* remove = std::get_if<RemoveStyleClass>(&operation)) {
        const std::optional<std::size_t> kPlace = placeOf(staged, remove->style);
        if (!kPlace.has_value()) {
            return kUnknown();
        }
        delta = StyleDelta{.kind = StyleDelta::Kind::DestroyClass,
                           .style = remove->style,
                           .place = *kPlace,
                           .before = classIn(staged, remove->style),
                           .after = std::nullopt};
    } else if (const auto* rename = std::get_if<RenameStyleClass>(&operation)) {
        if (!placeOf(staged, rename->style).has_value()) {
            return kUnknown();
        }
        StyleDelta named{.kind = StyleDelta::Kind::SetName, .style = rename->style};
        named.before = slotIn(staged, named);
        named.after = Value::string(rename->name);
        delta = std::move(named);
    } else if (const auto* timed = std::get_if<SetStyleTransitions>(&operation)) {
        if (!placeOf(staged, timed->style).has_value()) {
            return kUnknown();
        }
        StyleDelta transitions{.kind = StyleDelta::Kind::SetTransitions, .style = timed->style};
        transitions.before = slotIn(staged, transitions);
        transitions.after = timed->value;
        delta = std::move(transitions);
    } else {
        return refuse(result::ErrorClass::InvalidArgument,
                      AuthoringError::ValidationFailed,
                      "the operation changes a scene, not a style sheet");
    }
    if (same(delta->before, delta->after)) {
        return std::optional<StyleDelta>{};
    }
    return delta;
}

std::size_t bytesOf(const StyleDelta& delta) {
    const auto kWeigh = [](const std::optional<Value>& value) {
        return value.has_value() ? document::write(*value).size() : 0;
    };
    return sizeof(StyleDelta) + delta.variant.size() + delta.part.size() + kWeigh(delta.before) + kWeigh(delta.after);
}

} // namespace

result::Result<std::unique_ptr<AuthoredStyles>>
AuthoredStyles::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    RAWFRAME_TRY_ASSIGN(ui::StyleSheet sheet, sheetOf(std::string{text}));
    std::unique_ptr<AuthoredStyles> made{new AuthoredStyles()};
    made->identity_ = document;
    // What reads as a sheet is canonical JSON.
    made->root_ = *document::parse(text);
    made->sheet_ = std::move(sheet);
    made->history_ = History<Entry>{limits};
    return made;
}

std::string AuthoredStyles::text() const {
    return document::write(root_);
}

std::optional<Value> AuthoredStyles::part(std::uint64_t style, std::string_view variant, std::string_view part) const {
    return partIn(root_, style, variant, part);
}

result::Result<Committed> AuthoredStyles::change(std::uint64_t generation, std::span<const Operation> operations) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "a change is asked against the document's generation");
    }
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    Value staged = root_;
    std::vector<StyleDelta> journal;
    std::size_t bytes = 0;
    for (std::size_t at = 0; at < operations.size(); ++at) {
        const auto kAt = [at](std::unexpected<result::Error> refused) {
            return std::unexpected<result::Error>{std::move(refused).error().withContext("index", std::to_string(at))};
        };
        auto made = deltaOf(staged, operations[at]);
        if (!made.has_value()) {
            return kAt(std::unexpected<result::Error>{std::move(made).error()});
        }
        if (!made->has_value()) {
            continue;
        }
        StyleDelta delta = std::move(**made);
        RAWFRAME_TRY(applyTo(staged, delta, true));
        bytes += bytesOf(delta);
        journal.push_back(std::move(delta));
    }
    if (journal.empty()) {
        return Committed{.generation = generation_, .deltas = 0};
    }
    if (bytes > history_.limits().maximumJournalBytes) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a transaction's journal is past its limits");
    }
    RAWFRAME_TRY_ASSIGN(ui::StyleSheet sheet, sheetOf(document::write(staged)));
    root_ = std::move(staged);
    sheet_ = std::move(sheet);
    ++generation_;
    const std::size_t kDeltas = journal.size();
    history_.append(Entry{.journal = std::move(journal), .bytes = bytes});
    return Committed{.generation = generation_, .deltas = kDeltas};
}

result::Status AuthoredStyles::apply(const std::vector<StyleDelta>& journal, bool forward) {
    Value root = root_;
    for (std::size_t at = 0; at < journal.size(); ++at) {
        RAWFRAME_TRY(applyTo(root, journal[forward ? at : journal.size() - 1 - at], forward));
    }
    RAWFRAME_TRY_ASSIGN(ui::StyleSheet sheet, sheetOf(document::write(root)));
    root_ = std::move(root);
    sheet_ = std::move(sheet);
    return {};
}

result::Result<Committed> AuthoredStyles::step(std::uint64_t generation, bool undo) {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "an undo or redo is asked against the document's generation");
    }
    if (undo ? !canUndo() : !canRedo()) {
        return refuse(result::ErrorClass::NotFound,
                      AuthoringError::TargetNotFound,
                      undo ? "there is nothing to undo" : "there is nothing to redo");
    }
    const Entry& entry = history_.next(undo);
    auto applied = apply(entry.journal, !undo);
    if (!applied.has_value()) {
        // An entry is only ever applied to the sheet it was made against.
        return std::unexpected<result::Error>{std::move(applied).error().mappedTo(
            result::ErrorClass::Internal, kAuthoringDomain, code(AuthoringError::Internal), "a history entry applies")};
    }
    const std::size_t kDeltas = entry.journal.size();
    history_.stepped(undo);
    ++generation_;
    return Committed{.generation = generation_, .deltas = kDeltas};
}

std::string summaryOf(std::span<const StyleDelta> journal) {
    if (journal.empty()) {
        return "no change";
    }
    const StyleDelta& kFirst = journal.front();
    const auto kName = [](const std::optional<Value>& value, std::string_view member) {
        const Value* held = value.has_value() ? (member.empty() ? &*value : value->find(member)) : nullptr;
        return held != nullptr && held->text() != nullptr ? *held->text() : std::string{};
    };
    std::string made;
    switch (kFirst.kind) {
    case StyleDelta::Kind::SetPart:
        made = (kFirst.after.has_value() ? "set " : "unset ") + kFirst.variant + "." + kFirst.part;
        break;
    case StyleDelta::Kind::CreateClass:
        made = "create class " + kName(kFirst.after, "name");
        break;
    case StyleDelta::Kind::DestroyClass:
        made = "delete class " + kName(kFirst.before, "name");
        break;
    case StyleDelta::Kind::SetName:
        made = "rename class to " + kName(kFirst.after, "");
        break;
    case StyleDelta::Kind::SetTransitions:
        made = kFirst.after.has_value() ? "set transitions" : "unset transitions";
        break;
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

result::Result<Committed> AuthoredStyles::undo(std::uint64_t generation) {
    return step(generation, true);
}

result::Result<Committed> AuthoredStyles::redo(std::uint64_t generation) {
    return step(generation, false);
}

} // namespace rawframe::authoring
