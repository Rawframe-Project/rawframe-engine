// A game's JSON documents in Studio, listed after its scenes and followed
// by a played game (D634). A style sheet (D637): its classes in the entity
// column, made with New, deleted with Delete, and named in the name field
// (D638), and a chosen class's looks as fields, each part set through the
// session's `styles.set_part` (D636), then its transitions as one (D639).
// A UI document (D643): its nodes in the entity column, indented by how
// deep each stands, deleted with Delete, and a chosen node's fields, each
// set through `ui.set_field` (D641), and Up and Down moving it among its
// siblings (D644).

#include "rawframe/ui/document.h"
#include "rawframe/ui/styles.h"
#include "shell.h"

namespace rawframe::studio {

namespace {

/// The class `id` names in `sheet`, if it has one.
const Value* classOf(const std::optional<Value>& sheet, const std::string& id) {
    const Value* styles = sheet.has_value() ? sheet->find("styles") : nullptr;
    for (const Value& each : styles != nullptr ? styles->items() : std::span<const Value>{}) {
        const Value* held = each.find("styleId");
        if (held != nullptr && held->text() != nullptr && *held->text() == id) {
            return &each;
        }
    }
    return nullptr;
}

/// A part's value as a field shows it: a color or other text as it is,
/// anything else as JSON.
std::string partText(const Value& value) {
    return value.text() != nullptr ? *value.text() : document::writeCompact(value);
}

/// Typed text as a document holds it: nothing for none, JSON as it reads,
/// anything else as text, so a color is typed bare.
Value documentedValue(const std::string& text) {
    if (text.empty()) {
        return Value{};
    }
    auto read = document::parse(text);
    return read.has_value() ? std::move(*read) : Value::string(text);
}

/// Where node `component` stands: the node it is inside (none for a root),
/// its place among its siblings, and how many they are.
struct NodePlace {
    std::optional<std::string> parent;
    std::size_t place = 0;
    std::size_t siblings = 0;
};
std::optional<NodePlace>
placeOf(const Value* nodes, const std::string& component, const std::optional<std::string>& parent) {
    const std::span<const Value> kItems = nodes != nullptr ? nodes->items() : std::span<const Value>{};
    for (std::size_t at = 0; at < kItems.size(); ++at) {
        const Value* held = kItems[at].find("component");
        if (held == nullptr || held->text() == nullptr) {
            continue;
        }
        if (*held->text() == component) {
            return NodePlace{.parent = parent, .place = at, .siblings = kItems.size()};
        }
        if (auto inside = placeOf(kItems[at].find("children"), component, *held->text())) {
            return inside;
        }
    }
    return std::nullopt;
}

/// The node `component` names among `nodes` and the children inside them.
const Value* nodeOf(const Value* nodes, const std::string& component) {
    for (const Value& each : nodes != nullptr ? nodes->items() : std::span<const Value>{}) {
        const Value* held = each.find("component");
        if (held != nullptr && held->text() != nullptr && *held->text() == component) {
            return &each;
        }
        if (const Value* inside = nodeOf(each.find("children"), component)) {
            return inside;
        }
    }
    return nullptr;
}

} // namespace

void ShellParticipant::showSheet() {
    const std::optional<Value> kRead = read(scene_, "styles.read_sheet");
    sheet_ = kRead.has_value() && kRead->find("sheet") != nullptr ? std::optional<Value>{*kRead->find("sheet")}
                                                                  : std::nullopt;
    const Value* styles = sheet_.has_value() ? sheet_->find("styles") : nullptr;
    for (const Value& each : styles != nullptr ? styles->items() : std::span<const Value>{}) {
        const Value* id = each.find("styleId");
        const Value* name = each.find("name");
        if (id == nullptr || id->text() == nullptr) {
            continue;
        }
        const std::string kName = name != nullptr && name->text() != nullptr ? *name->text() : *id->text();
        auto added = row(entitiesColumn_, kName, kText, kRow, true);
        if (!added.has_value()) {
            break;
        }
        entityRows_.push_back(*added);
        entities_.push_back(*id->text());
        names_.push_back(kName);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
}

void ShellParticipant::showClass(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    if (entity_ != entities_[at]) {
        static_cast<void>(tree_->scrollTo(componentsColumn_, 0, 0));
    }
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* chosen = classOf(sheet_, entity_);
    if (chosen == nullptr) {
        return;
    }
    // Its name first, as an entity's is (D638).
    auto named = fieldRow("name", names_[at]);
    if (!named.has_value()) {
        return;
    }
    componentRows_.push_back(named->first);
    fields_.push_back(FieldRow{.value = named->second, .role = FieldRow::Role::Name, .field = "name"});
    // Each variant a heading, in the order a node's states pick them, and
    // under it every part a look has, set or not.
    for (const std::string_view kVariant : ui::kStyleVariantFields) {
        auto heading = row(componentsColumn_, kVariant, kQuiet, kPanel);
        if (!heading.has_value()) {
            return;
        }
        componentRows_.push_back(*heading);
        ++components_;
        const Value* look = chosen->find(kVariant);
        for (const std::string_view kPart : ui::kStyleLookFields) {
            const Value* held = look != nullptr ? look->find(kPart) : nullptr;
            auto line =
                fieldRow(kPart, held != nullptr ? partText(*held) : std::string{}, std::nullopt, held != nullptr);
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second,
                                       .role = FieldRow::Role::Part,
                                       .component = std::string{kVariant},
                                       .field = std::string{kPart}});
        }
    }
    // Last, its transitions, all of them as one field (D639).
    auto heading = row(componentsColumn_, "transitions", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    componentRows_.push_back(*heading);
    const Value* transitions = chosen->find("transitions");
    auto line = fieldRow("all",
                         transitions != nullptr ? document::writeCompact(*transitions) : std::string{},
                         std::nullopt,
                         transitions != nullptr);
    if (!line.has_value()) {
        return;
    }
    componentRows_.push_back(line->first);
    fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Part, .field = "transitions"});
}

void ShellParticipant::setPart(const FieldRow& row, const std::string& text) {
    Value value = documentedValue(text);
    Value operation = Value::object();
    if (row.component.empty()) {
        operation.add("operation", Value::string("styles.set_transitions"));
        operation.add("style", Value::string(entity_));
        operation.add("value", std::move(value));
        commit(std::move(operation), text.empty() ? "transitions unset" : "transitions set", entity_);
        return;
    }
    operation.add("operation", Value::string("styles.set_part"));
    operation.add("style", Value::string(entity_));
    operation.add("variant", Value::string(row.component));
    operation.add("part", Value::string(row.field));
    operation.add("value", std::move(value));
    commit(
        std::move(operation), row.component + " " + row.field + (text.empty() ? " unset" : " set to " + text), entity_);
}

void ShellParticipant::showUi() {
    const std::optional<Value> kRead = read(scene_, "ui.read_document");
    sheet_ = kRead.has_value() && kRead->find("document") != nullptr ? std::optional<Value>{*kRead->find("document")}
                                                                     : std::nullopt;
    // Each node before those inside it, as the document nests them.
    const auto kList = [this](const auto& self, const Value* nodes, std::size_t depth) -> void {
        for (const Value& each : nodes != nullptr ? nodes->items() : std::span<const Value>{}) {
            const Value* component = each.find("component");
            if (component == nullptr || component->text() == nullptr) {
                continue;
            }
            auto added = row(entitiesColumn_, std::string(2 * depth, ' ') + *component->text(), kText, kRow, true);
            if (!added.has_value()) {
                return;
            }
            entityRows_.push_back(*added);
            entities_.push_back(*component->text());
            names_.push_back(*component->text());
            brought_.push_back(false);
            removed_.push_back(false);
            places_.push_back(std::nullopt);
            self(self, each.find("children"), depth + 1);
        }
    };
    kList(kList, sheet_.has_value() ? sheet_->find("nodes") : nullptr, 0);
}

void ShellParticipant::showNode(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    if (entity_ != entities_[at]) {
        static_cast<void>(tree_->scrollTo(componentsColumn_, 0, 0));
    }
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* chosen = nodeOf(sheet_.has_value() ? sheet_->find("nodes") : nullptr, entity_);
    if (chosen == nullptr) {
        return;
    }
    // Every field a node has, in the document's order, set or not.
    for (const std::string_view kMember : ui::nodeMembers()) {
        if (kMember == "component" || kMember == "children") {
            continue;
        }
        const Value* held = chosen->find(kMember);
        auto line = fieldRow(kMember, held != nullptr ? partText(*held) : std::string{}, std::nullopt, held != nullptr);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::NodeField, .field = std::string{kMember}});
    }
    components_ = 1;
}

void ShellParticipant::setNodeField(const FieldRow& row, const std::string& text) {
    Value operation = Value::object();
    operation.add("operation", Value::string("ui.set_field"));
    operation.add("node", Value::string(entity_));
    operation.add("field", Value::string(row.field));
    operation.add("value", documentedValue(text));
    commit(std::move(operation), row.field + (text.empty() ? " unset" : " set to " + text), entity_);
}

void ShellParticipant::moveNode(int by) {
    const std::optional<NodePlace> kAt =
        placeOf(sheet_.has_value() ? sheet_->find("nodes") : nullptr, entity_, std::nullopt);
    if (!kAt.has_value() || (by < 0 && kAt->place == 0) || (by > 0 && kAt->place + 1 >= kAt->siblings)) {
        ++refused_;
        say(by < 0 ? "already first among its siblings" : "already last among its siblings");
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("ui.move_node"));
    operation.add("node", Value::string(entity_));
    operation.add("parent", kAt->parent.has_value() ? Value::string(*kAt->parent) : Value{});
    operation.add("place", Value::integer(static_cast<std::int64_t>(kAt->place) + by));
    commit(std::move(operation), by < 0 ? "moved up" : "moved down", entity_);
}

} // namespace rawframe::studio
