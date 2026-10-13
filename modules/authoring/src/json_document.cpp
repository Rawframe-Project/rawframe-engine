#include "rawframe/authoring/json_document.h"

#include "rawframe/authoring/errors.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

bool same(const std::optional<Value>& left, const std::optional<Value>& right) {
    return left.has_value() == right.has_value() &&
           (!left.has_value() || document::write(*left) == document::write(*right));
}

/// Where the item of `array` known by `step` stands, if it is there.
std::optional<std::size_t> itemOf(const Value* array, const JsonStep& step) {
    for (std::size_t at = 0; array != nullptr && at < array->items().size(); ++at) {
        const Value* key = array->items()[at].find(step.key);
        if (key != nullptr && key->text() != nullptr && *key->text() == step.id) {
            return at;
        }
    }
    return std::nullopt;
}

/// `object` with its member `name` as `value`, or gone for none, its
/// members in `order`. A container a change rebuilt (`rebuilt`) and left
/// empty is gone too, unless it is the root's own member.
Value withMember(const Value& object,
                 std::span<const std::string_view> order,
                 std::string_view name,
                 const std::optional<Value>& value,
                 bool rebuilt = false,
                 bool root = false) {
    const bool kEmpty = rebuilt && !root && value.has_value() &&
                        ((value->kind() == Value::Kind::Object && value->names().empty()) ||
                         (value->kind() == Value::Kind::Array && value->items().empty()));
    // A holder whose kind gives no order holds members of its own names, in
    // name order (an instance's overrides, D721).
    std::vector<std::string> named;
    if (order.empty()) {
        named.assign(object.names().begin(), object.names().end());
        if (std::ranges::find(named, name) == named.end()) {
            named.emplace_back(name);
        }
        std::ranges::sort(named);
    }
    std::vector<std::string_view> owned{named.begin(), named.end()};
    Value made = Value::object();
    for (const std::string_view kField : order.empty() ? std::span<const std::string_view>{owned} : order) {
        if (kField == name) {
            if (value.has_value() && !kEmpty) {
                made.add(std::string{kField}, *value);
            }
        } else if (const Value* held = object.find(kField)) {
            made.add(std::string{kField}, *held);
        }
    }
    return made;
}

/// `object`, at `path`'s first `depth` steps, with `to` at the rest of
/// `path` under it.
result::Result<Value> setIn(const Value& object,
                            std::span<const JsonStep> full,
                            std::size_t depth,
                            const std::optional<Value>& to,
                            std::size_t place,
                            MemberOrder order) {
    const std::span<const JsonStep> path = full.subspan(depth);
    const JsonStep& kStep = path.front();
    const std::span<const std::string_view> kOrder = order(full.first(depth));
    const bool kRoot = depth == 0;
    if (kStep.key.empty()) {
        if (path.size() == 1) {
            return withMember(object, kOrder, kStep.member, to);
        }
        const Value* held = object.find(kStep.member);
        RAWFRAME_TRY_ASSIGN(Value child,
                            setIn(held != nullptr ? *held : Value::object(), full, depth + 1, to, place, order));
        return withMember(object, kOrder, kStep.member, std::move(child), true, kRoot);
    }
    const Value* array = object.find(kStep.member);
    const std::size_t kCount = array != nullptr ? array->items().size() : 0;
    const std::optional<std::size_t> kFound = itemOf(array, kStep);
    Value items = Value::array();
    if (path.size() == 1) {
        // The item itself, which comes or goes whole at its place.
        const std::size_t kAt = kFound.value_or(place);
        if (kAt > kCount) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          AuthoringError::DeltaMismatch,
                          "an item's place is past its array's");
        }
        for (std::size_t each = 0; each <= kCount; ++each) {
            if (each == kAt && to.has_value()) {
                items.push(*to);
            }
            if (each < kCount && !(kFound.has_value() && each == *kFound)) {
                items.push(array->items()[each]);
            }
        }
    } else {
        if (!kFound.has_value()) {
            return refuse(
                result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the document has no such item");
        }
        for (std::size_t each = 0; each < kCount; ++each) {
            if (each == *kFound) {
                RAWFRAME_TRY_ASSIGN(Value item, setIn(array->items()[each], full, depth + 1, to, place, order));
                items.push(std::move(item));
            } else {
                items.push(array->items()[each]);
            }
        }
    }
    return withMember(object, kOrder, kStep.member, std::move(items), true, kRoot);
}

std::size_t bytesOf(const JsonDelta& delta) {
    std::size_t bytes = sizeof(JsonDelta);
    for (const JsonStep& kStep : delta.path) {
        bytes += sizeof(JsonStep) + kStep.member.size() + kStep.key.size() + kStep.id.size();
    }
    for (const std::optional<Value>* each : {&delta.before, &delta.after}) {
        bytes += each->has_value() ? document::write(**each).size() : 0;
    }
    return bytes;
}

} // namespace

std::optional<Value> slotAt(const Value& root, std::span<const JsonStep> path) {
    const Value* held = &root;
    for (const JsonStep& kStep : path) {
        held = held->find(kStep.member);
        if (held != nullptr && !kStep.key.empty()) {
            const std::optional<std::size_t> kAt = itemOf(held, kStep);
            held = kAt.has_value() ? &held->items()[*kAt] : nullptr;
        }
        if (held == nullptr) {
            return std::nullopt;
        }
    }
    return *held;
}

std::optional<std::size_t> placeAt(const Value& root, std::span<const JsonStep> path) {
    if (path.empty() || path.back().key.empty()) {
        return std::nullopt;
    }
    const std::optional<Value> kHolder = slotAt(root, path.first(path.size() - 1));
    return kHolder.has_value() ? itemOf(kHolder->find(path.back().member), path.back()) : std::nullopt;
}

result::Result<JsonDocument>
JsonDocument::open(std::string_view text, MemberOrder order, DocumentReader reads, const HistoryLimits& limits) {
    auto read = reads(text);
    if (!read.has_value()) {
        return std::unexpected<result::Error>{std::move(read).error()};
    }
    // What its kind reads is canonical JSON.
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error().mappedTo(result::ErrorClass::InvalidArgument,
                                                                                 kAuthoringDomain,
                                                                                 code(AuthoringError::ValidationFailed),
                                                                                 "a document reads")};
    }
    return JsonDocument{std::move(*parsed), order, reads, limits};
}

std::string JsonDocument::text() const {
    return document::write(root_);
}

result::Result<JsonDocument::Staged> JsonDocument::stage(std::uint64_t generation) const {
    if (generation != generation_) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "a change is asked against the document's generation");
    }
    return Staged{.root = root_, .journal = {}, .bytes = 0};
}

result::Status JsonDocument::apply(Staged& staged, JsonDelta delta) const {
    if (same(delta.before, delta.after)) {
        return {};
    }
    RAWFRAME_TRY(applyTo(staged.root, delta, true));
    staged.bytes += bytesOf(delta);
    staged.journal.push_back(std::move(delta));
    if (staged.journal.size() > history_.limits().maximumJournalDeltas ||
        staged.bytes > history_.limits().maximumJournalBytes) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a transaction's journal is past its limits");
    }
    return {};
}

result::Result<Committed> JsonDocument::keep(Staged staged) {
    if (staged.journal.empty()) {
        return Committed{.generation = generation_, .deltas = 0};
    }
    RAWFRAME_TRY(reads_(document::write(staged.root)));
    root_ = std::move(staged.root);
    ++generation_;
    const std::size_t kDeltas = staged.journal.size();
    history_.append(Entry{.journal = std::move(staged.journal), .bytes = staged.bytes});
    return Committed{.generation = generation_, .deltas = kDeltas};
}

result::Status JsonDocument::applyTo(Value& root, const JsonDelta& delta, bool forward) const {
    if (delta.path.empty()) {
        return refuse(result::ErrorClass::InvalidArgument, AuthoringError::DeltaInvalid, "a delta names a slot");
    }
    if (!same(slotAt(root, delta.path), forward ? delta.before : delta.after)) {
        return refuse(result::ErrorClass::FailedPrecondition,
                      AuthoringError::DeltaMismatch,
                      "a slot does not hold what its change leaves from");
    }
    RAWFRAME_TRY_ASSIGN(root, setIn(root, delta.path, 0, forward ? delta.after : delta.before, delta.place, order_));
    return {};
}

result::Result<Committed> JsonDocument::step(std::uint64_t generation, bool undo) {
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
    Value root = root_;
    for (std::size_t at = 0; at < entry.journal.size(); ++at) {
        auto applied = applyTo(root, entry.journal[undo ? entry.journal.size() - 1 - at : at], !undo);
        if (!applied.has_value()) {
            // An entry is only ever applied to the document it was made
            // against.
            return std::unexpected<result::Error>{std::move(applied).error().mappedTo(result::ErrorClass::Internal,
                                                                                      kAuthoringDomain,
                                                                                      code(AuthoringError::Internal),
                                                                                      "a history entry applies")};
        }
    }
    root_ = std::move(root);
    const std::size_t kDeltas = entry.journal.size();
    history_.stepped(undo);
    ++generation_;
    return Committed{.generation = generation_, .deltas = kDeltas};
}

result::Result<Committed> JsonDocument::undo(std::uint64_t generation) {
    return step(generation, true);
}

result::Result<Committed> JsonDocument::redo(std::uint64_t generation) {
    return step(generation, false);
}

} // namespace rawframe::authoring
