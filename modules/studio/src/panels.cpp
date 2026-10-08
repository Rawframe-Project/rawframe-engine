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
    // The heading's words at its start, and room for a button at its end.
    RAWFRAME_TRY_ASSIGN(lastHeading_,
                        box(kColumn,
                            ui::Layout{.height = ui::pixels(22),
                                       .justify = ui::Justify::SpaceBetween,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0},
                            0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(lastHeading_, ui::Layout{}, 0));
    RAWFRAME_TRY(words(kTitle, heading, kQuiet, 13));
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
    // Cook before Play, so Play, Undo, and Redo keep their places (D502).
    if (!cookTool_.empty()) {
        RAWFRAME_TRY_ASSIGN(cookNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(words(cookNode_, cooking_.has_value() ? "Stop" : "Cook", kText, 14));
    }
    if (play_.has_value()) {
        RAWFRAME_TRY_ASSIGN(playNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {12, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(words(playNode_, "Play", kText, 14));
    }
    RAWFRAME_TRY_ASSIGN(undoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
    RAWFRAME_TRY_ASSIGN(redoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
    RAWFRAME_TRY(showHistory());
    if (broken_.has_value()) {
        return showBroken();
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kColumns,
        box(root_,
            ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Row, .gap = 6, .grow = 1},
            kBackground));
    RAWFRAME_TRY_ASSIGN(scenesColumn_, column(kColumns, "Scenes"));
    RAWFRAME_TRY_ASSIGN(entitiesColumn_, column(kColumns, "Entities"));
    // The chosen entity copied whole (D458), its operations the catalog's.
    if (catalog_.offers("scene.create_entity") && catalog_.offers("scene.add_component") &&
        catalog_.offers("scene.set_field")) {
        // Moved to another scene, made there and deleted here (D498);
        // before Duplicate, which keeps its place at the heading's end.
        if (catalog_.offers("scene.destroy_entity")) {
            RAWFRAME_TRY_ASSIGN(moveNode_, box(lastHeading_, ui::Layout{.padding = {8, 2, 8, 2}}, kRow));
            RAWFRAME_TRY(words(moveNode_, "Move to", kText, 13));
        }
        RAWFRAME_TRY_ASSIGN(duplicateNode_, box(lastHeading_, ui::Layout{.padding = {8, 2, 8, 2}}, kRow));
        RAWFRAME_TRY(words(duplicateNode_, "Duplicate", kText, 13));
    }
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
        // An instance's entity: one it removed taken back, or the whole
        // instance removed (D450).
        if (catalog_.offers("scene.restore_entity")) {
            RAWFRAME_TRY_ASSIGN(restoreNode_, button(kTools, "Restore"));
        }
        if (catalog_.offers("scene.remove_instance")) {
            RAWFRAME_TRY_ASSIGN(uninstanceNode_, button(kTools, "Remove instance"));
        }
        // The chosen entity's place among the scene's own (D451).
        if (catalog_.offers("scene.move_entity")) {
            RAWFRAME_TRY_ASSIGN(upNode_, button(kTools, "Up"));
            RAWFRAME_TRY_ASSIGN(downNode_, button(kTools, "Down"));
        }
    }
    return showScenes();
}

result::Status ShellParticipant::showBroken() {
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kBroken,
        box(root_,
            ui::Layout{.direction = ui::Direction::Column, .gap = 8, .grow = 1, .padding = {16, 16, 16, 16}},
            kPanel));
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeading, box(kBroken, ui::Layout{.height = ui::pixels(24), .shrink = 0}, 0));
    RAWFRAME_TRY(words(kHeading, "The game does not read", kText, 18));
    RAWFRAME_TRY_ASSIGN(const ui::Node kWhy, box(kBroken, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(words(kWhy, *broken_, kQuiet, 15));
    if (diagnostic_.has_value()) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kWhere, box(kBroken, ui::Layout{.shrink = 0}, kField));
        RAWFRAME_TRY(words(kWhere,
                           diagnostic_->file + ":" + std::to_string(diagnostic_->line) + ":" +
                               std::to_string(diagnostic_->column) + "  " + diagnostic_->message,
                           kText,
                           15));
    }
    RAWFRAME_TRY_ASSIGN(const ui::Node kTools,
                        box(kBroken, ui::Layout{.height = ui::pixels(28), .gap = 6, .shrink = 0}, 0));
    // Only the game's own files are the author's to open (D453).
    if (diagnostic_.has_value() && diagnostic_->file.starts_with("game/") && !editor_.empty()) {
        RAWFRAME_TRY_ASSIGN(openNode_, button(kTools, "Open in editor"));
    }
    RAWFRAME_TRY_ASSIGN(retryNode_, button(kTools, "Retry"));
    say(*broken_);
    return {};
}

result::Status ShellParticipant::showScenes() {
    clear(sceneRows_);
    if (newScene_.has_value()) {
        static_cast<void>(tree_->remove(newSceneRow_));
        newScene_.reset();
    }
    for (std::size_t each = 0; each < scenes_.size(); ++each) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kRowNode,
            row(scenesColumn_, scenes_[each], kText, each == sceneAt_ && !scene_.empty() ? kChosen : kRow));
        sceneRows_.push_back(kRowNode);
    }
    // A new scene is the session's own verb, not an operation the catalog
    // lists, so it is always offered.
    // A file's absolute path typed there imports it instead (D505).
    RAWFRAME_TRY_ASSIGN(const auto kLine, fieldRow("new scene or import", "", scenesColumn_));
    newSceneRow_ = kLine.first;
    newScene_ = FieldRow{.value = kLine.second, .role = FieldRow::Role::NewScene, .field = "new scene"};
    if (!viewRows_.empty()) {
        showView();
        showViewText();
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
        // A scene's view is the session's, unknown here until it says,
        // and its search starts empty.
        view_.reset();
        find_.clear();
        static_cast<void>(tree_->scrollTo(entitiesColumn_, 0, 0));
        showView();
        if (preview_.has_value()) {
            attachPreview();
        }
    }
    for (std::size_t each = 0; each < sceneRows_.size(); ++each) {
        static_cast<void>(tree_->setLook(sceneRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(entityRows_);
    clear(sceneFieldRows_);
    sceneFields_.clear();
    clear(componentRows_);
    actions_.clear();
    entities_.clear();
    names_.clear();
    brought_.clear();
    removed_.clear();
    places_.clear();
    entity_.clear();
    components_ = 0;
    findField_.reset();
    const std::optional<Value> kList = entitiesShown();
    const Value* listed = kList.has_value() ? kList->find("entities") : nullptr;
    if (listed == nullptr || listed->kind() != Value::Kind::Array) {
        return;
    }
    for (const Value& each : listed->items()) {
        const Value* id = each.find("id");
        const Value* name = each.find("name");
        const Value* brought = each.find("brought");
        const Value* removed = each.find("removed");
        const bool kRemoved = removed != nullptr && removed->truth().value_or(false);
        if (id == nullptr || id->text() == nullptr) {
            continue;
        }
        std::string label = name != nullptr && name->text() != nullptr ? *name->text() : std::string{};
        if (label.empty() && brought != nullptr) {
            const Value* instance = brought->find("instance");
            label = "instance " + (instance != nullptr ? document::writeCompact(*instance) : std::string{"?"}) + "  " +
                    id->text()->substr(0, 8);
        }
        if (kRemoved) {
            label += "  (removed)";
        }
        auto added = row(entitiesColumn_, label, brought != nullptr ? kQuiet : kText);
        if (!added.has_value()) {
            break;
        }
        entityRows_.push_back(*added);
        entities_.push_back(*id->text());
        names_.push_back(name != nullptr && name->text() != nullptr ? *name->text() : std::string{});
        brought_.push_back(brought != nullptr);
        removed_.push_back(kRemoved);
        const Value* place = each.find("place");
        places_.push_back(place != nullptr ? place->integer() : std::nullopt);
    }
    // Under the scene's entities, the search that chose them (D473), then
    // the scene an instance placed in it is of.
    if (catalog_.offers("scene.find_entities")) {
        auto line = fieldRow("find", find_, entitiesColumn_);
        if (line.has_value()) {
            sceneFieldRows_.push_back(line->first);
            findField_ = FieldRow{.value = line->second, .role = FieldRow::Role::Find, .field = "find"};
        }
    }
    if (catalog_.offers("scene.add_instance")) {
        auto line = fieldRow("instance of", "", entitiesColumn_);
        if (line.has_value()) {
            sceneFieldRows_.push_back(line->first);
            sceneFields_.push_back(
                FieldRow{.value = line->second, .role = FieldRow::Role::Instance, .field = "instance of"});
        }
    }
}

std::optional<Value> ShellParticipant::entitiesShown() {
    if (find_.empty()) {
        return read(scene_, "scene.list_entities");
    }
    const Search kSearch = searchOf(find_);
    std::string having;
    if (!kSearch.having.empty()) {
        std::string why;
        const std::optional<Catalog::Component> kComponent = componentNamed(catalog_, kSearch.having, why);
        if (!kComponent.has_value()) {
            // Nothing found rather than everything: the search is still
            // shown, and what it lacks said.
            say("find: " + why);
            Value none = Value::object();
            none.add("entities", Value::array());
            return none;
        }
        having = kComponent->id;
    }
    std::optional<Value> found = firstAnswer(ask(findRecord(next(), scene_, kSearch.named, having)));
    const Value* entities = found.has_value() ? found->find("entities") : nullptr;
    if (entities != nullptr && entities->kind() == Value::Kind::Array) {
        ++searched_;
        emitter_.log(diagnostics::Severity::Info,
                     kSearched,
                     "the entity column shows what a search found",
                     {diagnostics::field("found", static_cast<std::uint64_t>(entities->items().size())),
                      diagnostics::field("named", kSearch.named),
                      diagnostics::field("having", kSearch.having)});
    }
    return found;
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
    // And marked in its preview, where it stands (D464).
    markChosen();
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
        const Value* stale = each.find("stale");
        if (!componentHeading(name != nullptr && name->text() != nullptr ? *name->text() : "?",
                              kComponent,
                              brought_[at],
                              stale != nullptr && stale->truth().value_or(false))
                 .has_value()) {
            return;
        }
        ++components_;
        for (const FieldShown& kEach : fieldsShown(catalog_.component(kComponent), each.find("fields"))) {
            // A reference shows the name of the entity it names, if it has one.
            std::string text = kEach.text.value_or(std::string{});
            if (kEach.kind == "entity") {
                const auto kAt = std::ranges::find(entities_, text);
                if (kAt != entities_.end() && !names_[static_cast<std::size_t>(kAt - entities_.begin())].empty()) {
                    text = names_[static_cast<std::size_t>(kAt - entities_.begin())];
                }
            }
            // A number naming a declared asset shows the asset (D455).
            if (kEach.kind == "unsigned") {
                if (const Asset* held = assetHeld(assets_, text); held != nullptr) {
                    text = assetShown(*held);
                }
            }
            auto line = fieldRow(kEach.name, text, std::nullopt, kEach.text.has_value());
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

result::Status
ShellParticipant::componentHeading(std::string_view name, const std::string& component, bool brought, bool stale) {
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
    // The heading's buttons together at its end.
    RAWFRAME_TRY_ASSIGN(const ui::Node kActions, box(kHeading, ui::Layout{.alignItems = ui::Align::Center}, 0));
    // A component the scene was authored against another layout of is
    // carried over to the catalog's, for every entity of the scene (D452).
    if (stale && catalog_.offers("scene.remark_component")) {
        RAWFRAME_TRY(action(kActions,
                            "Update",
                            ActionButton{.operation = "scene.remark_component",
                                         .component = component,
                                         .done = std::string{name} + " updated",
                                         .whole = true}));
    }
    const std::string_view kOperation = brought ? "scene.revert_component" : "scene.remove_component";
    if (catalog_.offers(kOperation)) {
        RAWFRAME_TRY(action(kActions,
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
    for (const std::string_view kPart : {"eye", "target", "fieldOfView", "snap"}) {
        auto line = fieldRow(kPart == "fieldOfView" ? "field of view" : kPart, {}, scenesColumn_);
        if (!line.has_value()) {
            return;
        }
        viewRows_.push_back(line->first);
        viewFields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::View, .field = std::string{kPart}});
    }
    showViewText();
    showHistoryList();
}

void ShellParticipant::showHistoryList() {
    clear(historyRows_);
    entryRows_.clear();
    if (scene_.empty()) {
        return;
    }
    auto heading = row(scenesColumn_, "History", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    historyRows_.push_back(*heading);
    for (const HistoryEntry& entry : historyOf(ask(historyRecord(next(), scene_)))) {
        auto added = row(scenesColumn_, entry.summary, entry.applied ? kText : kQuiet, entry.applied ? kRow : kField);
        if (!added.has_value()) {
            return;
        }
        historyRows_.push_back(*added);
        entryRows_.push_back(*added);
    }
}

void ShellParticipant::showViewText() {
    for (const FieldRow& kViewField : viewFields_) {
        static_cast<void>(words(kViewField.value,
                                kViewField.field == "snap" ? snapText(snap_) : viewText(view_, kViewField.field),
                                kText,
                                14));
    }
}

result::Status ShellParticipant::showHistory() {
    RAWFRAME_TRY(words(undoNode_, "Undo", undoable_ > 0 ? kText : kQuiet, 14));
    return words(redoNode_, "Redo", redoable_ > 0 ? kText : kQuiet, 14);
}

void ShellParticipant::say(std::string text) {
    // Logged as it changes: a game that ended is said again every half
    // second, and was logged as often (D495).
    const bool kChanged = text != status_;
    status_ = std::move(text);
    static_cast<void>(words(statusNode_, status_, kQuiet, 14));
    if (kChanged && !status_.empty()) {
        emitter_.log(diagnostics::Severity::Info,
                     kSaid,
                     "Studio's status line says",
                     {diagnostics::field("status", std::string_view{status_})});
    }
}

} // namespace rawframe::studio
