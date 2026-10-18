// A game's JSON documents in Studio, listed after its scenes and followed
// by a played game (D634). A style sheet (D637): its classes in the entity
// column, made with New, deleted with Delete, and named in the name field
// (D638), and a chosen class's looks as fields, each part set through the
// session's `styles.set_part` (D636), then its transitions as one (D639).
// A UI document (D643): its nodes in the entity column, indented by how
// deep each stands, deleted with Delete, and a chosen node's fields, each
// set through `ui.set_field` (D641), Up and Down moving it among its
// siblings (D644), and Move to moving it into the node pressed next, or to
// the document's top where the document's row is (D671). New offers the
// game's node components the document does not hold, one made at its top
// as its row is pressed (D672). A material instance (D722): its base's
// inputs and switches in the entity column, and a chosen one's type,
// default, and override, the override set through
// `materials.set_override` (D721). A material graph (D730): its nodes in
// the entity column, a chosen one's params and inputs as fields, each set
// through `materials.set_node_param` or `materials.set_node_input` (D728),
// and Delete removing it.

#include "hints.h"
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
    if (sheet_.has_value()) {
        showSheetPreview(*sheet_);
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
    markUiPreview();
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
            // Who else has it chosen, or holds it, when joined (D886).
            auto added = row(entitiesColumn_,
                             std::string(2 * depth, ' ') + *component->text() + presenceMarks(*component->text()),
                             kText,
                             kRow,
                             true);
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
    if (sheet_.has_value()) {
        showUiPreview(*sheet_);
    }
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
    markUiPreview();
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

void ShellParticipant::moveNodeInto(const std::optional<std::string>& parent) {
    const std::optional<NodePlace> kAt =
        placeOf(sheet_.has_value() ? sheet_->find("nodes") : nullptr, entity_, std::nullopt);
    // Last among its new siblings; the session refuses a node moved into
    // one inside it.
    if (!kAt.has_value() || parent == entity_ || (kAt->parent == parent && kAt->place + 1 == kAt->siblings)) {
        ++refused_;
        say(!kAt.has_value()    ? "nothing to move"
            : parent == entity_ ? "a node is not moved inside itself"
                                : "already there");
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("ui.move_node"));
    operation.add("node", Value::string(entity_));
    operation.add("parent", parent.has_value() ? Value::string(*parent) : Value{});
    commit(std::move(operation), parent.has_value() ? "moved into " + *parent : "moved to the top", entity_);
}

void ShellParticipant::showNodeForm() {
    endEdit();
    fields_.clear();
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* nodes = sheet_.has_value() ? sheet_->find("nodes") : nullptr;
    std::size_t offered = 0;
    for (const Catalog::Component& kComponent : catalog_.components) {
        if (!kComponent.uiNode || nodeOf(nodes, kComponent.name) != nullptr) {
            continue;
        }
        if (offered == 0) {
            auto heading = row(componentsColumn_, "make a node of", kQuiet, kPanel);
            if (!heading.has_value()) {
                return;
            }
            componentRows_.push_back(*heading);
        }
        auto line = row(componentsColumn_, kComponent.name, kText, kRow, true);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(*line);
        actions_.push_back(ActionButton{
            .node = *line, .component = kComponent.name, .done = kComponent.name + " made", .makesNode = true});
        ++offered;
    }
    say(offered == 0 ? "every node component the game declares is in the document" : "choose the node's component");
}

void ShellParticipant::showGraphForm() {
    endEdit();
    fields_.clear();
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* types = nodeTypes_.has_value() ? nodeTypes_->find("types") : nullptr;
    if (types == nullptr || types->items().empty()) {
        say("the material module lists no node types");
        return;
    }
    auto heading = row(componentsColumn_, "make a node of", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    componentRows_.push_back(*heading);
    for (const Value& kType : types->items()) {
        const Value* named = kType.find("type");
        const std::string kName = named != nullptr && named->text() != nullptr ? *named->text() : std::string{};
        auto line = row(componentsColumn_, kName, kText, kRow, true);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(*line);
        actions_.push_back(ActionButton{.node = *line, .component = kName, .done = kName + " made", .makesNode = true});
    }
    say("choose the node's type");
}

void ShellParticipant::showMaterial() {
    // The instance and its base's interface, as one answer holds them.
    sheet_ = read(scene_, "materials.read_instance");
    const Value* instance = sheet_.has_value() ? sheet_->find("instance") : nullptr;
    const Value* overrides = instance != nullptr ? instance->find("overrides") : nullptr;
    const Value* interface = sheet_.has_value() ? sheet_->find("interface") : nullptr;
    for (const std::string_view kKind : {"inputs", "switches"}) {
        const Value* held = interface != nullptr ? interface->find(kKind) : nullptr;
        for (const std::string& kName : held != nullptr ? held->names() : std::span<const std::string>{}) {
            const bool kSet = overrides != nullptr && overrides->find(kName) != nullptr;
            auto added = row(entitiesColumn_, kName, kSet ? kText : kQuiet, kRow, true);
            if (!added.has_value()) {
                return;
            }
            entityRows_.push_back(*added);
            entities_.push_back(kName);
            names_.push_back(std::string{kKind});
            brought_.push_back(false);
            removed_.push_back(false);
            places_.push_back(std::nullopt);
        }
    }
    if (interface == nullptr || interface->kind() != Value::Kind::Object) {
        say("the instance's base does not read as a material");
    }
}

void ShellParticipant::showInput(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* interface = sheet_.has_value() ? sheet_->find("interface") : nullptr;
    const Value* kind = interface != nullptr ? interface->find(names_[at]) : nullptr;
    const Value* declared = kind != nullptr ? kind->find(entity_) : nullptr;
    const Value* instance = sheet_.has_value() ? sheet_->find("instance") : nullptr;
    const Value* overrides = instance != nullptr ? instance->find("overrides") : nullptr;
    const Value* held = overrides != nullptr ? overrides->find(entity_) : nullptr;
    // A switch is a bool its base's graph branches on (D719).
    const Value* type = declared != nullptr ? declared->find("type") : nullptr;
    const Value* fallback = declared != nullptr ? declared->find("default") : nullptr;
    for (const std::string& kLine :
         {names_[at] == "switches" ? std::string{"switch"} : (type != nullptr ? partText(*type) : std::string{}),
          "default " + (fallback != nullptr ? partText(*fallback) : std::string{})}) {
        auto line = row(componentsColumn_, kLine, kQuiet, kPanel);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(*line);
    }
    auto line = fieldRow("override", held != nullptr ? partText(*held) : std::string{}, std::nullopt, held != nullptr);
    if (!line.has_value()) {
        return;
    }
    componentRows_.push_back(line->first);
    fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Override, .field = entity_});
    components_ = 1;
}

void ShellParticipant::setOverride(const std::string& text) {
    Value operation = Value::object();
    operation.add("operation", Value::string("materials.set_override"));
    operation.add("input", Value::string(entity_));
    operation.add("value", documentedValue(text));
    commit(std::move(operation), entity_ + (text.empty() ? " unset" : " set to " + text), entity_);
}

void ShellParticipant::showGraph() {
    if (!nodeTypes_.has_value()) {
        nodeTypes_ = read(scene_, "materials.list_node_types");
    }
    const std::optional<Value> kRead = read(scene_, "materials.read_graph");
    sheet_ = kRead.has_value() && kRead->find("graph") != nullptr ? std::optional<Value>{*kRead->find("graph")}
                                                                  : std::nullopt;
    const Value* nodes = sheet_.has_value() ? sheet_->find("graph") : nullptr;
    for (std::size_t at = 0; nodes != nullptr && at < nodes->names().size(); ++at) {
        const std::string& kId = nodes->names()[at];
        const Value* type = nodes->items()[at].find("type");
        const std::string kType = type != nullptr && type->text() != nullptr ? *type->text() : std::string{};
        auto added = row(entitiesColumn_, kId.substr(0, 8) + "  " + kType, kText, kRow, true);
        if (!added.has_value()) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(kId);
        names_.push_back(kType);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    if (nodes != nullptr) {
        showCanvas(*nodes, "");
    }
}

void ShellParticipant::showGraphNode(std::size_t at) {
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
    markCanvas();
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    const Value* nodes = sheet_.has_value() ? sheet_->find("graph") : nullptr;
    const Value* chosen = nodes != nullptr ? nodes->find(entity_) : nullptr;
    if (chosen == nullptr) {
        return;
    }
    // Its params, then its inputs, each under a heading: those its type's
    // signature names and those the graph holds, in name order, an absent
    // one an unset field (D732).
    const Value* types = nodeTypes_.has_value() ? nodeTypes_->find("types") : nullptr;
    const Value* signature = nullptr;
    for (const Value& kType : types != nullptr ? types->items() : std::span<const Value>{}) {
        const Value* named = kType.find("type");
        if (named != nullptr && named->text() != nullptr && *named->text() == names_[at]) {
            signature = &kType;
        }
    }
    for (const auto& [kMember, kRole] : {std::pair{std::string_view{"params"}, FieldRow::Role::GraphParam},
                                         std::pair{std::string_view{"inputs"}, FieldRow::Role::GraphInput}}) {
        const Value* held = chosen->find(kMember);
        const Value* listed = signature != nullptr ? signature->find(kMember) : nullptr;
        std::vector<std::string> names;
        if (held != nullptr) {
            names.assign(held->names().begin(), held->names().end());
        }
        for (const Value& kName : listed != nullptr ? listed->items() : std::span<const Value>{}) {
            if (kName.text() != nullptr && !std::ranges::contains(names, *kName.text())) {
                names.push_back(*kName.text());
            }
        }
        std::ranges::sort(names);
        if (names.empty()) {
            continue;
        }
        auto heading = row(componentsColumn_, kMember, kQuiet, kPanel);
        if (!heading.has_value()) {
            return;
        }
        componentRows_.push_back(*heading);
        ++components_;
        for (const std::string& kName : names) {
            const Value* given = held != nullptr ? held->find(kName) : nullptr;
            auto line =
                fieldRow(kName, given != nullptr ? partText(*given) : std::string{}, std::nullopt, given != nullptr);
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second, .role = kRole, .field = kName});
        }
    }
    // Then its outputs, each pressed to connect it (D733).
    const Value* outputs = signature != nullptr ? signature->find("outputs") : nullptr;
    if (outputs == nullptr || outputs->items().empty()) {
        return;
    }
    auto heading = row(componentsColumn_, "outputs", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    componentRows_.push_back(*heading);
    for (const Value& kOutput : outputs->items()) {
        const std::string kName = kOutput.text() != nullptr ? *kOutput.text() : std::string{};
        auto line = row(componentsColumn_, kName, kText, kRow, true);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(*line);
        actions_.push_back(ActionButton{.node = *line, .field = kName, .connects = true});
    }
}

void ShellParticipant::connectGraphInput(const FieldRow& row, const std::pair<std::string, std::string>& from) {
    Value connection = Value::object();
    connection.add("node", Value::string(from.first));
    connection.add("output", Value::string(from.second));
    Value operation = Value::object();
    operation.add("operation", Value::string("materials.set_node_input"));
    operation.add("node", Value::string(entity_));
    operation.add("input", Value::string(row.field));
    operation.add("value", std::move(connection));
    commit(std::move(operation), row.field + " connected", entity_);
}

void ShellParticipant::setGraphMember(const FieldRow& row, const std::string& text) {
    const bool kParam = row.role == FieldRow::Role::GraphParam;
    Value operation = Value::object();
    operation.add("operation", Value::string(kParam ? "materials.set_node_param" : "materials.set_node_input"));
    operation.add("node", Value::string(entity_));
    operation.add(kParam ? "param" : "input", Value::string(row.field));
    operation.add("value", documentedValue(text));
    commit(std::move(operation), row.field + (text.empty() ? " unset" : " set to " + text), entity_);
}

} // namespace rawframe::studio
