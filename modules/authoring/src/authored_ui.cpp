#include "rawframe/authoring/authored_ui.h"

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

/// The document's objects' members in order, by where they are: the root,
/// and a node in `nodes` or `children`.
std::span<const std::string_view> orderOf(std::span<const JsonStep> at) noexcept {
    if (at.empty()) {
        return ui::kUiDocumentFields;
    }
    return ui::nodeMembers();
}

result::Result<ui::UiDocument> readOf(std::string_view text) {
    auto read = ui::readDocument(text);
    if (!read.has_value()) {
        // What the reader refused, and where, said to whoever asked.
        std::string why = "the UI document does not read: " + std::string{read.error().description()};
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
    RAWFRAME_TRY(readOf(text));
    return {};
}

/// The path to node `node` under `held`'s `member`, found depth first.
std::optional<JsonPath> found(const Value& held, std::string_view member, std::string_view node, JsonPath& path) {
    const Value* items = held.find(member);
    for (const Value& kItem : items != nullptr ? items->items() : std::span<const Value>{}) {
        const Value* component = kItem.find("component");
        if (component == nullptr || component->text() == nullptr) {
            continue;
        }
        path.push_back(
            JsonStep{.member = std::string{member}, .key = "component", .id = *component->text(), .place = {}});
        if (*component->text() == node) {
            return path;
        }
        if (auto inside = found(kItem, "children", node, path)) {
            return inside;
        }
        path.pop_back();
    }
    return std::nullopt;
}

std::optional<JsonPath> nodeIn(const Value& root, std::string_view node) {
    JsonPath path;
    return found(root, "nodes", node, path);
}

/// `record`, the node `node`, put inside `parent` or among the roots, at
/// `place` among its siblings or after them.
result::Result<JsonDelta> insertion(const Value& staged,
                                    Value record,
                                    const std::string& node,
                                    const std::optional<std::string>& parent,
                                    std::optional<std::size_t> place) {
    JsonPath path;
    if (parent.has_value()) {
        std::optional<JsonPath> held = nodeIn(staged, *parent);
        if (!held.has_value()) {
            return refuse(
                result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the document has no such node");
        }
        path = std::move(*held);
    }
    const std::optional<Value> kHolder = slotAt(staged, path);
    const Value* siblings = kHolder.has_value() ? kHolder->find(path.empty() ? "nodes" : "children") : nullptr;
    const std::size_t kCount = siblings != nullptr ? siblings->items().size() : 0;
    if (place.value_or(kCount) > kCount) {
        return refuse(result::ErrorClass::InvalidArgument,
                      AuthoringError::ValidationFailed,
                      "a node's place is among its siblings or after them");
    }
    path.push_back(
        JsonStep{.member = path.empty() ? "nodes" : "children", .key = "component", .id = node, .place = {}});
    return JsonDelta{
        .path = std::move(path), .place = place.value_or(kCount), .before = std::nullopt, .after = std::move(record)};
}

/// The delta `operation` makes of `staged`; refused where it cannot be
/// made.
result::Result<JsonDelta> deltaOf(const Value& staged, const Operation& operation) {
    const auto kUnknown = [] {
        return refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the document has no such node");
    };
    if (const auto* set = std::get_if<SetUiField>(&operation)) {
        const std::span<const std::string_view> kMembers = ui::nodeMembers();
        if (set->field == "component" || set->field == "children" ||
            std::ranges::find(kMembers, set->field) == kMembers.end()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a field is one a node has, not its component or children");
        }
        std::optional<JsonPath> path = nodeIn(staged, set->node);
        if (!path.has_value()) {
            return kUnknown();
        }
        path->push_back(JsonStep{.member = set->field, .key = {}, .id = {}, .place = {}});
        std::optional<Value> before = slotAt(staged, *path);
        return JsonDelta{.path = std::move(*path), .place = 0, .before = std::move(before), .after = set->value};
    }
    if (const auto* create = std::get_if<CreateUiNode>(&operation)) {
        if (nodeIn(staged, create->node).has_value()) {
            return refuse(
                result::ErrorClass::AlreadyExists, AuthoringError::Conflict, "a component is one node's in a document");
        }
        Value record = Value::object();
        record.add("component", Value::string(create->node));
        return insertion(staged, std::move(record), create->node, create->parent, create->place);
    }
    if (const auto* remove = std::get_if<RemoveUiNode>(&operation)) {
        std::optional<JsonPath> path = nodeIn(staged, remove->node);
        if (!path.has_value()) {
            return kUnknown();
        }
        const std::size_t kPlace = placeAt(staged, *path).value_or(0);
        std::optional<Value> before = slotAt(staged, *path);
        return JsonDelta{.path = std::move(*path), .place = kPlace, .before = std::move(before), .after = std::nullopt};
    }
    return refuse(result::ErrorClass::InvalidArgument,
                  AuthoringError::ValidationFailed,
                  "the operation does not change a UI document");
}

} // namespace

result::Result<std::unique_ptr<AuthoredUi>>
AuthoredUi::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    RAWFRAME_TRY_ASSIGN(ui::UiDocument read, readOf(text));
    RAWFRAME_TRY_ASSIGN(JsonDocument opened, JsonDocument::open(text, orderOf, reads, limits));
    return std::unique_ptr<AuthoredUi>{new AuthoredUi(document, std::move(opened), std::move(read))};
}

std::optional<JsonPath> AuthoredUi::pathOf(std::string_view node) const {
    return nodeIn(document_.root(), node);
}

result::Result<Committed> AuthoredUi::change(std::uint64_t generation, std::span<const Operation> operations) {
    RAWFRAME_TRY_ASSIGN(JsonDocument::Staged staged, document_.stage(generation));
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    for (std::size_t at = 0; at < operations.size(); ++at) {
        if (const auto* move = std::get_if<MoveUiNode>(&operations[at])) {
            // Taken out, then put where it goes in what is left: two deltas,
            // each found in the document as the one before left it.
            auto moved = stageMove(staged, *move);
            if (!moved.has_value()) {
                return std::unexpected<result::Error>{
                    std::move(moved).error().withContext("index", std::to_string(at))};
            }
            continue;
        }
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

result::Status AuthoredUi::stageMove(JsonDocument::Staged& staged, const MoveUiNode& move) const {
    const std::array<Operation, 1> kTaken = {RemoveUiNode{.node = move.node}};
    RAWFRAME_TRY_ASSIGN(JsonDelta taken, deltaOf(staged.root, kTaken[0]));
    if (move.parent.has_value()) {
        const std::optional<JsonPath> kParent = nodeIn(staged.root, *move.parent);
        if (kParent.has_value() && std::ranges::find(*kParent, move.node, &JsonStep::id) != kParent->end()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "a node is not moved inside itself");
        }
    }
    Value record = *taken.before;
    RAWFRAME_TRY(document_.apply(staged, std::move(taken)));
    RAWFRAME_TRY_ASSIGN(JsonDelta put, insertion(staged.root, std::move(record), move.node, move.parent, move.place));
    return document_.apply(staged, std::move(put));
}

result::Result<Committed> AuthoredUi::reread(result::Result<Committed> done) {
    if (done.has_value() && done->deltas > 0) {
        // What the document's reader has just read reads again.
        read_ = *readOf(document_.text());
    }
    return done;
}

result::Result<Committed> AuthoredUi::undo(std::uint64_t generation) {
    return reread(document_.undo(generation));
}

result::Result<Committed> AuthoredUi::redo(std::uint64_t generation) {
    return reread(document_.redo(generation));
}

std::string AuthoredUi::summaryAt(std::size_t entry) const {
    const std::vector<JsonDelta>& journal = document_.journalAt(entry);
    if (journal.empty()) {
        return "no change";
    }
    const JsonDelta& kFirst = journal.front();
    std::string made;
    std::size_t more = journal.size() - 1;
    if (journal.size() > 1 && !kFirst.after.has_value() && !journal[1].before.has_value() &&
        !kFirst.path.back().key.empty() && kFirst.path.back().id == journal[1].path.back().id) {
        // Taken out and put back elsewhere: a move.
        made = "move node " + kFirst.path.back().id;
        --more;
    } else if (!kFirst.path.back().key.empty()) {
        made = (kFirst.after.has_value() ? "create node " : "delete node ") + kFirst.path.back().id;
    } else {
        const std::string& kNode = kFirst.path[kFirst.path.size() - 2].id;
        made = (kFirst.after.has_value() ? "set " : "unset ") + kFirst.path.back().member + " of " + kNode;
    }
    if (more > 0) {
        made += " and " + std::to_string(more) + " more";
    }
    return made;
}

result::Result<Value> answer(const AuthoredUi& document, const Query& query) {
    if (!std::holds_alternative<ReadUiDocument>(query)) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     "the query does not read a UI document")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value made = Value::object();
    made.add("document", document.documentValue());
    return made;
}

} // namespace rawframe::authoring
