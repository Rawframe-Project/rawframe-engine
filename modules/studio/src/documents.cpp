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
            // Who else has it chosen, or holds it, when joined (D886); a
            // tree row as the design's, what the node is after its name
            // (D923).
            const Value* kChildren = each.find("children");
            const bool kParent = kChildren != nullptr && !kChildren->items().empty();
            const std::string_view kKind = each.find("text") != nullptr    ? "text"
                                           : each.find("image") != nullptr ? "image"
                                           : each.find("shows") != nullptr ? "shows"
                                           : kParent                       ? "box"
                                                                           : "node";
            const std::string& kComponent = *component->text();
            const std::size_t kDot = kComponent.find_last_of('.');
            auto added = entityRow(entitiesColumn_,
                                   (kDot == std::string::npos ? kComponent : kComponent.substr(kDot + 1)) +
                                       presenceMarks(kComponent),
                                   depth,
                                   kParent,
                                   kKind == "text"    ? Icon::Script
                                   : kKind == "image" ? Icon::Texture
                                                      : Icon::Ui,
                                   look::kEntityInk,
                                   look::kRowInk,
                                   kKind);
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
    // A node chosen at once, so the Inspector says what it is.
    if (!entities_.empty() && (entity_.empty() || !std::ranges::contains(entities_, entity_))) {
        showNode(0);
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
    // Its name, then every field a node has in the document's order: those
    // it sets first, then the rest (D923).
    if (auto head =
            box(componentsColumn_,
                ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {12, 12, 10, 8}},
                0);
        head.has_value()) {
        componentRows_.push_back(*head);
        if (auto name = box(*head, ui::Layout{.shrink = 0}, 0); name.has_value()) {
            static_cast<void>(strongWords(*name, entity_, look::kInk, 15));
        }
    }
    for (const bool kSetHere : {true, false}) {
        if (auto heading = box(componentsColumn_,
                               ui::Layout{.alignItems = ui::Align::Center,
                                          .gap = 8,
                                          .shrink = 0,
                                          .padding = {10, 10, 10, 6},
                                          .border = {0, 0, 1, 0}},
                               0);
            heading.has_value()) {
            componentRows_.push_back(*heading);
            static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = look::kLine}));
            if (auto titled = box(*heading, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
                static_cast<void>(strongWords(*titled, kSetHere ? "Set" : "Everything else", look::kInk));
            }
            if (auto quiet = box(*heading, ui::Layout{.shrink = 1}, 0); quiet.has_value()) {
                static_cast<void>(monoWords(*quiet,
                                            kSetHere ? "what this node says" : "at their defaults, typed to set",
                                            look::kFaintInk,
                                            look::kSmallSize));
            }
        }
        for (const std::string_view kMember : ui::nodeMembers()) {
            if (kMember == "component" || kMember == "children") {
                continue;
            }
            const Value* held = chosen->find(kMember);
            if ((held != nullptr) != kSetHere) {
                continue;
            }
            auto line = fieldRow(kMember,
                                 held != nullptr ? partText(*held) : std::string{},
                                 std::nullopt,
                                 held != nullptr,
                                 prettyField(kMember));
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            // What an identity names, under its field (D941).
            if (const std::string kNamed = held != nullptr && (kMember == "text" || kMember == "font" ||
                                                               kMember == "image" || kMember == "style")
                                               ? uiNamed(kMember, partText(*held))
                                               : std::string{};
                !kNamed.empty()) {
                if (auto named = box(componentsColumn_, ui::Layout{.shrink = 0, .padding = {136, 10, 0, 4}}, 0);
                    named.has_value()) {
                    componentRows_.push_back(*named);
                    static_cast<void>(words(*named, kNamed, look::kSoftInk, look::kSmallSize));
                }
            }
            fields_.push_back(
                FieldRow{.value = line->second, .role = FieldRow::Role::NodeField, .field = std::string{kMember}});
        }
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
            // As a graph's rows (D923): a dot of its kind, a texture's or a
            // value's, then its name, those set brighter.
            const Value* declared = held->find(kName);
            const Value* type = declared != nullptr ? declared->find("type") : nullptr;
            Value kinded = Value::object();
            kinded.add("type",
                       Value::string(type != nullptr && type->text() != nullptr && type->text()->starts_with("texture")
                                         ? "rawframe/parameter_texture_2d@1"
                                         : "rawframe/parameter@1"));
            auto added = box(entitiesColumn_,
                             ui::Layout{.height = ui::pixels(look::kRowHeight),
                                        .alignItems = ui::Align::Center,
                                        .gap = 8,
                                        .shrink = 0,
                                        .padding = {10, 8, 0, 0}},
                             kRow);
            if (!added.has_value()) {
                return;
            }
            if (auto dot = box(*added, ui::Layout{.width = ui::pixels(8), .height = ui::pixels(8), .shrink = 0}, 0);
                dot.has_value()) {
                static_cast<void>(tree_->setLook(*dot, ui::Look{.fill = graphNodeDot(kinded), .radius = 2}));
            }
            if (auto said = box(*added, ui::Layout{.shrink = 1}, 0); said.has_value()) {
                static_cast<void>(words(*said, kName, kSet ? look::kInk : look::kRowInk, look::kBodySize));
                static_cast<void>(
                    tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
            static_cast<void>(tree_->setRole(*added, ui::Role::Button));
            static_cast<void>(tree_->setName(*added, kName));
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
    showInstanceCard();
    showInstanceSheet();
}

void ShellParticipant::showInstanceCard() {
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    const Value* instance = sheet_.has_value() ? sheet_->find("instance") : nullptr;
    const Value* base = instance != nullptr ? instance->find("base") : nullptr;
    const std::string kBase = base != nullptr && base->text() != nullptr ? *base->text() : std::string{};
    // In the center, where a graph's canvas is: what the instance is, its
    // look on a sphere as the last cook drew it, and its base to open
    // (D927).
    auto middle = box(centerColumn_,
                      ui::Layout{.height = ui::pixels(0),
                                 .direction = ui::Direction::Column,
                                 .justify = ui::Justify::Center,
                                 .alignItems = ui::Align::Center,
                                 .grow = 1,
                                 .shrink = 1},
                      0x15161AFFU);
    if (!middle.has_value()) {
        return;
    }
    viewRows_.push_back(*middle);
    auto card = box(*middle,
                    ui::Layout{.width = ui::pixels(340),
                               .direction = ui::Direction::Column,
                               .alignItems = ui::Align::Center,
                               .gap = 10,
                               .shrink = 0,
                               .padding = {20, 20, 18, 18},
                               .border = {1, 1, 1, 1}},
                    0);
    if (!card.has_value()) {
        return;
    }
    static_cast<void>(tree_->setLook(*card, ui::Look{.fill = 0x121317F0U, .borderColor = look::kEdge, .radius = 8}));
    const auto kSource = std::ranges::find(browser_.sources, scene_, &ListedSource::path);
    const std::shared_ptr<const texture::Texture> kPicture =
        kSource != browser_.sources.end() ? thumbnailOf(*kSource) : nullptr;
    if (kPicture != nullptr) {
        if (auto image = box(*card, ui::Layout{.width = ui::pixels(180), .height = ui::pixels(180), .shrink = 0}, 0);
            image.has_value()) {
            static_cast<void>(tree_->setLook(*image, ui::Look{.image = thumbnailKey(scene_)}));
        }
    }
    if (auto name = box(*card, ui::Layout{.shrink = 0}, 0); name.has_value()) {
        static_cast<void>(strongWords(*name, std::filesystem::path{scene_}.filename().string(), look::kInk, 15));
    }
    if (auto what = box(*card, ui::Layout{.width = ui::share(1), .shrink = 0}, 0); what.has_value()) {
        static_cast<void>(wrappedWords(*what,
                                       kBase.empty() ? std::string{"A material instance whose base is not named."}
                                                     : "An instance of " + kBase +
                                                           ": it keeps the base's graph and sets its parameters, "
                                                           "each in the Inspector.",
                                       look::kLabelInk,
                                       look::kLabelSize));
    }
    if (kPicture == nullptr) {
        if (auto later = box(*card, ui::Layout{.shrink = 0}, 0); later.has_value()) {
            static_cast<void>(words(*later, "Its preview after a cook", look::kFaintInk, look::kLabelSize));
        }
    }
    if (!kBase.empty() && std::ranges::contains(scenes_, kBase)) {
        if (auto opened = textButton(*card, Icon::Material, "Open " + kBase); opened.has_value()) {
            actions_.push_back(ActionButton{.node = *opened, .opens = kBase});
        }
    }
}

void ShellParticipant::showInstanceSheet() {
    endEdit();
    fields_.clear();
    clear(componentRows_);
    std::erase_if(actions_, [](const ActionButton& action) {
        return action.opens.empty();
    });
    components_ = 0;
    const Value* interface = sheet_.has_value() ? sheet_->find("interface") : nullptr;
    const Value* instance = sheet_.has_value() ? sheet_->find("instance") : nullptr;
    const Value* overrides = instance != nullptr ? instance->find("overrides") : nullptr;
    const Value* base = instance != nullptr ? instance->find("base") : nullptr;
    // As the design's Inspector (D923): the instance's name and what it
    // is, then every parameter at once, a field each: an override set, or
    // its base's default shown faint.
    if (auto head =
            box(componentsColumn_,
                ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {12, 12, 10, 8}},
                0);
        head.has_value()) {
        componentRows_.push_back(*head);
        if (auto name = box(*head, ui::Layout{.shrink = 0}, 0); name.has_value()) {
            static_cast<void>(strongWords(*name, std::filesystem::path{scene_}.filename().string(), look::kInk, 15));
        }
        if (auto what = box(*head, ui::Layout{.shrink = 0}, 0); what.has_value()) {
            static_cast<void>(
                monoWords(*what,
                          "material instance" +
                              (base != nullptr && base->text() != nullptr ? " of " + *base->text() : std::string{}),
                          look::kFaintInk,
                          look::kSmallSize));
        }
    }
    for (std::size_t at = 0; at < entities_.size(); ++at) {
        if (at == 0 || names_[at] != names_[at - 1]) {
            auto heading = box(componentsColumn_,
                               ui::Layout{.alignItems = ui::Align::Center,
                                          .gap = 8,
                                          .shrink = 0,
                                          .padding = {10, 10, 10, 6},
                                          .border = {0, 0, 1, 0}},
                               0);
            if (!heading.has_value()) {
                return;
            }
            componentRows_.push_back(*heading);
            static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = look::kLine}));
            if (auto titled = box(*heading, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
                static_cast<void>(
                    strongWords(*titled, names_[at] == "switches" ? "Switches" : "Parameters", look::kInk));
            }
            if (auto quieted = box(*heading, ui::Layout{.shrink = 1}, 0); quieted.has_value()) {
                static_cast<void>(monoWords(*quieted, "empty keeps the base's", look::kFaintInk, look::kSmallSize));
            }
        }
        const Value* kind = interface != nullptr ? interface->find(names_[at]) : nullptr;
        const Value* declared = kind != nullptr ? kind->find(entities_[at]) : nullptr;
        const Value* fallback = declared != nullptr ? declared->find("default") : nullptr;
        const Value* held = overrides != nullptr ? overrides->find(entities_[at]) : nullptr;
        const std::string kHint = held == nullptr && fallback != nullptr ? partText(*fallback) : std::string{};
        auto line = fieldRow(
            entities_[at], held != nullptr ? partText(*held) : kHint, std::nullopt, held != nullptr, entities_[at]);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::Override, .field = entities_[at], .hint = kHint});
    }
    components_ = 1;
}

void ShellParticipant::showInput(std::size_t at) {
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    // Every parameter is in the Inspector already: the chosen one's field
    // takes the keys.
    if (at < fields_.size()) {
        beginEdit(fields_[at]);
    }
}

void ShellParticipant::setOverride(const std::string& input, const std::string& text) {
    Value operation = Value::object();
    operation.add("operation", Value::string("materials.set_override"));
    operation.add("input", Value::string(input));
    operation.add("value", documentedValue(text));
    commit(std::move(operation), input + (text.empty() ? " unset" : " set to " + text), input);
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
        // As the design's (D923): its kind's dot and its name in words.
        auto added = box(entitiesColumn_,
                         ui::Layout{.height = ui::pixels(look::kRowHeight),
                                    .alignItems = ui::Align::Center,
                                    .gap = 8,
                                    .shrink = 0,
                                    .padding = {10, 8, 0, 0}},
                         kRow);
        if (!added.has_value()) {
            return;
        }
        if (auto dot = box(*added, ui::Layout{.width = ui::pixels(8), .height = ui::pixels(8), .shrink = 0}, 0);
            dot.has_value()) {
            static_cast<void>(tree_->setLook(*dot, ui::Look{.fill = graphNodeDot(nodes->items()[at]), .radius = 2}));
        }
        const std::string kLabel = graphNodeLabel(nodes->items()[at]);
        if (auto said = box(*added, ui::Layout{.shrink = 1}, 0); said.has_value()) {
            static_cast<void>(words(*said, kLabel, look::kRowInk, look::kBodySize));
            static_cast<void>(tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        static_cast<void>(tree_->setRole(*added, ui::Role::Button));
        static_cast<void>(tree_->setName(*added, kLabel + " " + kId.substr(0, 8)));
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
    // The material as its last cook drew it, on a sphere, in the canvas's
    // corner as the design's preview (D923).
    const auto kSource = std::ranges::find(browser_.sources, scene_, &ListedSource::path);
    const std::shared_ptr<const texture::Texture> kPicture =
        kSource != browser_.sources.end() ? thumbnailOf(*kSource) : nullptr;
    auto corner = box(
        centerColumn_,
        ui::Layout{.padding = {0, 16, 0, 16},
                   .placement = {.absolute = true, .x = ui::share(1), .y = ui::share(1), .anchorX = 1, .anchorY = 1}},
        0);
    if (corner.has_value()) {
        viewRows_.push_back(*corner);
        static_cast<void>(tree_->setInteraction(*corner, ui::Interaction{.hits = ui::Interaction::Hits::Children}));
    }
    if (!corner.has_value()) {
        return;
    }
    if (auto card = box(*corner,
                        ui::Layout{.width = ui::pixels(192),
                                   .direction = ui::Direction::Column,
                                   .gap = 6,
                                   .padding = {10, 10, 8, 10},
                                   .border = {1, 1, 1, 1}},
                        0);
        card.has_value()) {
        static_cast<void>(
            tree_->setLook(*card, ui::Look{.fill = 0x121317F0U, .borderColor = look::kEdge, .radius = 6}));
        if (auto said = box(*card, ui::Layout{.shrink = 0}, 0); said.has_value()) {
            static_cast<void>(words(*said,
                                    kPicture != nullptr ? "Preview \u00B7 sphere" : "Preview after a cook",
                                    look::kLabelInk,
                                    look::kLabelSize));
        }
        if (kPicture != nullptr) {
            if (auto image =
                    box(*card, ui::Layout{.width = ui::pixels(170), .height = ui::pixels(170), .shrink = 0}, 0);
                image.has_value()) {
                static_cast<void>(tree_->setLook(*image, ui::Look{.image = thumbnailKey(scene_)}));
            }
        }
    }
    // A node chosen at once, the Inspector saying what it is: the
    // material's output, as the design opens on it.
    if (entity_.empty() || !std::ranges::contains(entities_, entity_)) {
        const auto kOutput = std::ranges::find_if(names_, [](const std::string& type) {
            return type.find("surface") != std::string::npos || type.find("output") != std::string::npos;
        });
        if (!entities_.empty()) {
            showGraphNode(kOutput != names_.end() ? static_cast<std::size_t>(kOutput - names_.begin()) : 0);
        }
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
    // As the design's (D923): the node's name and what it is, then its
    // params and inputs under headings, a wired input saying what it is
    // wired from, then its outputs, each pressed to connect it (D733).
    const auto kSection = [this](std::string_view title, std::string_view quiet) {
        auto heading = box(componentsColumn_,
                           ui::Layout{.alignItems = ui::Align::Center,
                                      .gap = 8,
                                      .shrink = 0,
                                      .padding = {10, 10, 10, 6},
                                      .border = {0, 0, 1, 0}},
                           0);
        if (!heading.has_value()) {
            return false;
        }
        componentRows_.push_back(*heading);
        static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = look::kLine}));
        if (auto titled = box(*heading, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
            static_cast<void>(strongWords(*titled, title, look::kInk));
        }
        if (auto quieted = box(*heading, ui::Layout{.shrink = 1}, 0); quieted.has_value()) {
            static_cast<void>(monoWords(*quieted, quiet, look::kFaintInk, look::kSmallSize));
        }
        return true;
    };
    if (auto head =
            box(componentsColumn_,
                ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {12, 12, 10, 8}},
                0);
        head.has_value()) {
        componentRows_.push_back(*head);
        if (auto name = box(*head, ui::Layout{.shrink = 0}, 0); name.has_value()) {
            static_cast<void>(strongWords(*name, graphNodeLabel(*chosen), look::kInk, 15));
        }
        if (auto what = box(*head, ui::Layout{.shrink = 0}, 0); what.has_value()) {
            static_cast<void>(monoWords(*what, names_[at], look::kFaintInk, look::kSmallSize));
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
        if (!kSection(kMember == "params" ? "Node" : "Inputs",
                      kMember == "params" ? "its settings" : "a constant, or a wire from another node")) {
            return;
        }
        ++components_;
        for (const std::string& kName : names) {
            const Value* given = held != nullptr ? held->find(kName) : nullptr;
            const Value* kFrom =
                given != nullptr && given->kind() == Value::Kind::Object ? given->find("node") : nullptr;
            if (kFrom != nullptr && kFrom->text() != nullptr) {
                // Wired: from which node's which output, which it says; a
                // constant typed over it unwires it, and an output pressed
                // then it wires it anew.
                const Value* kSource = nodes->find(*kFrom->text());
                const Value* kOutput = given->find("output");
                const std::string kWired =
                    "\u2190 " + (kSource != nullptr ? graphNodeLabel(*kSource) : *kFrom->text()) +
                    (kOutput != nullptr && kOutput->text() != nullptr ? " \u00B7 " + *kOutput->text() : std::string{});
                auto line = fieldRow(kName, kWired, std::nullopt, true, prettyField(kName));
                if (!line.has_value()) {
                    return;
                }
                static_cast<void>(words(line->second, kWired, look::kAccent, look::kBodySize));
                componentRows_.push_back(line->first);
                fields_.push_back(FieldRow{.value = line->second, .role = kRole, .field = kName, .kind = kWired});
                continue;
            }
            auto line = fieldRow(kName,
                                 given != nullptr ? partText(*given) : std::string{},
                                 std::nullopt,
                                 given != nullptr,
                                 prettyField(kName));
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second, .role = kRole, .field = kName});
        }
    }
    const Value* outputs = signature != nullptr ? signature->find("outputs") : nullptr;
    if (outputs == nullptr || outputs->items().empty()) {
        return;
    }
    if (!kSection("Outputs", "press one, then an input")) {
        return;
    }
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

void ShellParticipant::dropWire() {
    const std::optional<std::pair<std::string, std::string>> kFrom = std::exchange(held_.wiring, std::nullopt);
    const std::optional<std::array<float, 2>> kPointer = releasedAt_.has_value() ? releasedAt_ : pointerLaidOut();
    if (!kFrom.has_value() || !kPointer.has_value() || !chosen(Listed::Graph) ||
        std::hypot((*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]) < kDragAfter) {
        return;
    }
    for (const EditorsHeld::CanvasPort& kInput : held_.canvasInputs) {
        if (!tree_->contains(kInput.line) || kInput.entity >= entities_.size()) {
            continue;
        }
        const ui::Rect kAt = tree_->placedRectOf(kInput.line);
        if ((*kPointer)[0] < kAt.x || (*kPointer)[1] < kAt.y || (*kPointer)[0] >= kAt.x + kAt.width ||
            (*kPointer)[1] >= kAt.y + kAt.height) {
            continue;
        }
        const std::string& kNode = entities_[kInput.entity];
        if (kNode == kFrom->first) {
            ++refused_;
            say("a node's output is not wired to its own input");
            return;
        }
        Value connection = Value::object();
        connection.add("node", Value::string(kFrom->first));
        connection.add("output", Value::string(kFrom->second));
        Value operation = Value::object();
        operation.add("operation", Value::string("materials.set_node_input"));
        operation.add("node", Value::string(kNode));
        operation.add("input", Value::string(kInput.name));
        operation.add("value", std::move(connection));
        commit(std::move(operation), kInput.name + " connected", kNode);
        return;
    }
}

void ShellParticipant::setGraphMember(const FieldRow& row, const std::string& text) {
    // A wired input left saying what it is wired from is left wired.
    if (!row.kind.empty() && text == row.kind) {
        return;
    }
    const bool kParam = row.role == FieldRow::Role::GraphParam;
    Value operation = Value::object();
    operation.add("operation", Value::string(kParam ? "materials.set_node_param" : "materials.set_node_input"));
    operation.add("node", Value::string(entity_));
    operation.add(kParam ? "param" : "input", Value::string(row.field));
    operation.add("value", documentedValue(text));
    commit(std::move(operation), row.field + (text.empty() ? " unset" : " set to " + text), entity_);
}

} // namespace rawframe::studio
