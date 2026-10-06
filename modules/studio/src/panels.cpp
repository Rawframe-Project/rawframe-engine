#include "shell.h"

namespace rawframe::studio {

result::Result<ui::Node>
ShellParticipant::box(std::optional<ui::Node> parent, const ui::Layout& layout, std::uint32_t color) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kNode, tree_->add(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kNode, layout));
    RAWFRAME_TRY(tree_->setLook(kNode, ui::Look{.fill = color, .radius = 4}));
    if (parent.has_value()) {
        RAWFRAME_TRY(tree_->attach(*parent, kNode));
    }
    return kNode;
}

result::Status ShellParticipant::words(ui::Node node, std::string_view text, std::uint32_t color, float size) {
    return tree_->setText(node, text, ui::TextLook{.font = font_, .size = size, .color = color, .wrap = false});
}

result::Result<ui::Node> ShellParticipant::column(ui::Node parent, std::string_view heading) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kColumn,
                        box(parent,
                            ui::Layout{.width = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .grow = 1,
                                       .padding = {8, 8, 8, 8},
                                       .scroll = ui::Scroll::Vertical},
                            kPanel));
    // A column scrolls what does not fit (D441), so its rows keep their
    // heights rather than shrinking to fit.
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeading, box(kColumn, ui::Layout{.height = ui::pixels(22), .shrink = 0}, 0));
    RAWFRAME_TRY(words(kHeading, heading, kQuiet, 13));
    return kColumn;
}

result::Result<ui::Node> ShellParticipant::button(ui::Node parent, std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kButton, box(parent, ui::Layout{.padding = {10, 4, 10, 4}}, kRow));
    RAWFRAME_TRY(words(kButton, text, kText, 14));
    return kButton;
}

result::Status ShellParticipant::build() {
    RAWFRAME_TRY_ASSIGN(root_,
                        box(std::nullopt,
                            ui::Layout{.width = ui::share(1),
                                       .height = ui::share(1),
                                       .direction = ui::Direction::Column,
                                       .gap = 6,
                                       .padding = {6, 6, 6, 6}},
                            kBackground));
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeader,
                        box(root_,
                            ui::Layout{.height = ui::pixels(34),
                                       .justify = ui::Justify::SpaceBetween,
                                       .alignItems = ui::Align::Center,
                                       .padding = {12, 4, 6, 4}},
                            kHeaderFill));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeader, ui::Layout{.padding = {0, 2, 0, 2}}, 0));
    RAWFRAME_TRY(words(kTitle, title_, kText));
    // The status, then undo and redo, at the header's end.
    RAWFRAME_TRY_ASSIGN(const ui::Node kEnd, box(kHeader, ui::Layout{.alignItems = ui::Align::Center, .gap = 6}, 0));
    RAWFRAME_TRY_ASSIGN(statusNode_, box(kEnd, ui::Layout{.padding = {0, 2, 8, 2}}, 0));
    if (play_.has_value()) {
        RAWFRAME_TRY_ASSIGN(playNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {12, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(words(playNode_, "Play", kText, 14));
    }
    RAWFRAME_TRY_ASSIGN(undoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
    RAWFRAME_TRY_ASSIGN(redoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
    RAWFRAME_TRY(showHistory());
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kColumns,
        box(root_,
            ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Row, .gap = 6, .grow = 1},
            kBackground));
    RAWFRAME_TRY_ASSIGN(scenesColumn_, column(kColumns, "Scenes"));
    RAWFRAME_TRY_ASSIGN(entitiesColumn_, column(kColumns, "Entities"));
    RAWFRAME_TRY_ASSIGN(componentsColumn_, column(kColumns, "Components"));
    // The entities' operations above their rows.
    if (catalog_.offers("scene.create_entity") || catalog_.offers("scene.destroy_entity")) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kTools,
                            box(entitiesColumn_, ui::Layout{.height = ui::pixels(28), .gap = 6, .shrink = 0}, 0));
        if (catalog_.offers("scene.create_entity")) {
            RAWFRAME_TRY_ASSIGN(newNode_, button(kTools, "New"));
        }
        if (catalog_.offers("scene.destroy_entity")) {
            RAWFRAME_TRY_ASSIGN(deleteNode_, button(kTools, "Delete"));
        }
    }
    for (std::size_t each = 0; each < scenes_.size(); ++each) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode, row(scenesColumn_, scenes_[each], kText));
        sceneRows_.push_back(kRowNode);
    }
    return {};
}

result::Result<ui::Node>
ShellParticipant::row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                        box(column, ui::Layout{.height = ui::pixels(28), .shrink = 0, .padding = {8, 4, 8, 4}}, fill));
    RAWFRAME_TRY(words(kRowNode, text, color, 14));
    return kRowNode;
}

void ShellParticipant::clear(std::vector<ui::Node>& rows) {
    for (const ui::Node kRowNode : rows) {
        static_cast<void>(tree_->remove(kRowNode));
    }
    rows.clear();
}

void ShellParticipant::showScene(std::size_t at) {
    endEdit();
    fields_.clear();
    const bool kChanged = scene_ != scenes_[at];
    if (kChanged) {
        // Each scene its own history.
        undoable_ = 0;
        redoable_ = 0;
        static_cast<void>(showHistory());
        if (previewing_) {
            static_cast<void>(ask(previewRecord(next(), scene_, nullptr)));
            previewing_ = false;
        }
    }
    scene_ = scenes_[at];
    sceneAt_ = at;
    if (kChanged) {
        // A scene's view is the session's, unknown here until it says.
        view_.reset();
        static_cast<void>(tree_->scrollTo(entitiesColumn_, 0, 0));
        showView();
        if (preview_.has_value()) {
            const Answered kAttached = answeredOf(ask(previewRecord(next(), scene_, &*preview_)));
            previewing_ = kAttached.previewing;
            if (kAttached.view.has_value()) {
                view_ = kAttached.view;
            }
            say(kAttached.done ? (previewing_ ? "previewing " + scene_ : "no preview") : kAttached.message);
            showViewText();
        }
    }
    for (std::size_t each = 0; each < sceneRows_.size(); ++each) {
        static_cast<void>(tree_->setLook(sceneRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(entityRows_);
    clear(componentRows_);
    actions_.clear();
    entities_.clear();
    names_.clear();
    brought_.clear();
    entity_.clear();
    components_ = 0;
    const std::optional<Value> kList = read(scene_, "scene.list_entities");
    const Value* listed = kList.has_value() ? kList->find("entities") : nullptr;
    if (listed == nullptr || listed->kind() != Value::Kind::Array) {
        return;
    }
    for (const Value& each : listed->items()) {
        const Value* id = each.find("id");
        const Value* name = each.find("name");
        const Value* brought = each.find("brought");
        if (id == nullptr || id->text() == nullptr) {
            continue;
        }
        std::string label = name != nullptr && name->text() != nullptr ? *name->text() : std::string{};
        if (label.empty() && brought != nullptr) {
            const Value* instance = brought->find("instance");
            label = "instance " + (instance != nullptr ? document::writeCompact(*instance) : std::string{"?"}) + "  " +
                    id->text()->substr(0, 8);
        }
        auto added = row(entitiesColumn_, label, brought != nullptr ? kQuiet : kText);
        if (!added.has_value()) {
            break;
        }
        entityRows_.push_back(*added);
        entities_.push_back(*id->text());
        names_.push_back(name != nullptr && name->text() != nullptr ? *name->text() : std::string{});
        brought_.push_back(brought != nullptr);
    }
}

void ShellParticipant::showEntity(std::size_t at) {
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
    // Chosen in the session, so undo and redo keep it (D417).
    static_cast<void>(ask(selectRecord(next(), scene_, entity_)));
    // The scene's own entity is named; an instance's is its source's.
    if (catalog_.offers("scene.rename_entity") && !brought_[at]) {
        auto line = fieldRow("name", names_[at]);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Name, .field = "name"});
    }
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    const bool kListed = components != nullptr && components->kind() == Value::Kind::Array;
    for (const Value& each : kListed ? components->items() : std::span<const Value>{}) {
        const Value* name = each.find("name");
        const Value* component = each.find("component");
        const std::string kComponent =
            component != nullptr && component->text() != nullptr ? *component->text() : std::string{};
        if (!componentHeading(
                 name != nullptr && name->text() != nullptr ? *name->text() : "?", kComponent, brought_[at])
                 .has_value()) {
            return;
        }
        ++components_;
        for (const FieldShown& kEach : fieldsShown(catalog_.component(kComponent), each.find("fields"))) {
            auto line = fieldRow(kEach.name, kEach.text.value_or(std::string{}), std::nullopt, kEach.text.has_value());
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            // An instance's own value, which it may drop for its source's.
            if (brought_[at] && kEach.text.has_value() && catalog_.offers("scene.revert_field")) {
                static_cast<void>(action(line->first,
                                         "Revert",
                                         ActionButton{.operation = "scene.revert_field",
                                                      .component = kComponent,
                                                      .field = kEach.name,
                                                      .done = kEach.name + " reverted"}));
            }
            fields_.push_back(
                FieldRow{.value = line->second, .component = kComponent, .field = kEach.name, .kind = kEach.kind});
        }
    }
    if (catalog_.offers("scene.add_component")) {
        auto line = fieldRow("add", "");
        if (line.has_value()) {
            componentRows_.push_back(line->first);
            fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::Add, .field = "add"});
        }
    }
}

result::Status ShellParticipant::componentHeading(std::string_view name, const std::string& component, bool brought) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeading,
                        box(componentsColumn_,
                            ui::Layout{.height = ui::pixels(28),
                                       .justify = ui::Justify::SpaceBetween,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0,
                                       .padding = {8, 0, 2, 0}},
                            kRow));
    componentRows_.push_back(kHeading);
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeading, ui::Layout{.padding = {0, 4, 0, 4}}, 0));
    RAWFRAME_TRY(words(kTitle, name, kText, 14));
    const std::string_view kOperation = brought ? "scene.revert_component" : "scene.remove_component";
    if (catalog_.offers(kOperation)) {
        RAWFRAME_TRY(action(kHeading,
                            brought ? "Revert" : "Remove",
                            ActionButton{.operation = std::string{kOperation},
                                         .component = component,
                                         .done = std::string{name} + (brought ? " reverted" : " removed")}));
    }
    return {};
}

result::Status ShellParticipant::action(ui::Node row, std::string_view text, ActionButton asked) {
    RAWFRAME_TRY_ASSIGN(asked.node, box(row, ui::Layout{.padding = {8, 2, 8, 2}, .margin = {0, 2, 0, 2}}, kPanel));
    RAWFRAME_TRY(words(asked.node, text, kQuiet, 13));
    actions_.push_back(std::move(asked));
    return {};
}

result::Result<std::pair<ui::Node, ui::Node>>
ShellParticipant::fieldRow(std::string_view name, std::string_view value, std::optional<ui::Node> column, bool set) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kLine,
                        box(column.value_or(componentsColumn_),
                            ui::Layout{.height = ui::pixels(28),
                                       .direction = ui::Direction::Row,
                                       .alignItems = ui::Align::Center,
                                       .gap = 8,
                                       .shrink = 0,
                                       .padding = {8, 2, 8, 2}},
                            kPanel));
    RAWFRAME_TRY_ASSIGN(const ui::Node kName,
                        box(kLine, ui::Layout{.width = ui::pixels(120), .padding = {0, 2, 0, 2}}, 0));
    // A field the scene sets reads brighter than one at its default.
    RAWFRAME_TRY(words(kName, name, set ? kText : kQuiet, 14));
    RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kValue, ui::Layout{.height = ui::pixels(22), .grow = 1, .padding = {6, 2, 6, 2}}));
    RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{.fill = kField, .radius = 3}));
    RAWFRAME_TRY(tree_->attach(kLine, kValue));
    RAWFRAME_TRY(words(kValue, value, kText, 14));
    return std::pair{kLine, kValue};
}

void ShellParticipant::showView() {
    clear(viewRows_);
    viewFields_.clear();
    auto heading = row(scenesColumn_, "View", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    viewRows_.push_back(*heading);
    for (const std::string_view kPart : {"eye", "target", "fieldOfView"}) {
        auto line = fieldRow(kPart == "fieldOfView" ? "field of view" : kPart, {}, scenesColumn_);
        if (!line.has_value()) {
            return;
        }
        viewRows_.push_back(line->first);
        viewFields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::View, .field = std::string{kPart}});
    }
    showViewText();
}

void ShellParticipant::showViewText() {
    for (const FieldRow& kViewField : viewFields_) {
        static_cast<void>(words(kViewField.value, viewText(view_, kViewField.field), kText, 14));
    }
}

result::Status ShellParticipant::showHistory() {
    RAWFRAME_TRY(words(undoNode_, "Undo", undoable_ > 0 ? kText : kQuiet, 14));
    return words(redoNode_, "Redo", redoable_ > 0 ? kText : kQuiet, 14);
}

void ShellParticipant::say(std::string text) {
    status_ = std::move(text);
    static_cast<void>(words(statusNode_, status_, kQuiet, 14));
}

} // namespace rawframe::studio
