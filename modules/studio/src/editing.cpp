#include "hints.h"
#include "shell.h"

#include <algorithm>
#include <cstdlib>

namespace rawframe::studio {

std::string ShellParticipant::ask(const Value& record) {
    // On the start screen there is no game to ask of (D781).
    if (!gameOpen()) {
        return {};
    }
    ++records_;
    return answerLine(document::writeCompact(record));
}

std::int64_t ShellParticipant::next() const noexcept {
    return static_cast<std::int64_t>(records_ + 1);
}

std::optional<Value>
ShellParticipant::read(const std::string& scene, std::string_view operation, std::string_view entity) {
    return firstAnswer(ask(readRecord(next(), scene, operation, entity)));
}

void ShellParticipant::pressAt(float x, float y) {
    // A release no frame has taken yet is the last press's, taken before
    // this one is (D833).
    if (released_) {
        followSplitter();
    }
    pressedAt_ = {x, y};
    const bool kMoving = beforePress();
    const auto kHit = tree_->hit(root_, x, y);
    heldEntity_.reset();
    if (kHit.has_value() && kHit->node.has_value()) {
        // Only a pointer's press holds a row to drag; a screen reader's
        // press is let go as it is made.
        pointerPress_ = true;
        pressOn(*kHit->node, kMoving);
        pointerPress_ = false;
    }
}

bool ShellParticipant::beforePress() {
    // Keys read before the press are taken before it, in the order they
    // came (D489): a field's text submitted, and the history row it adds,
    // are there for the press that follows them in one frame.
    if (!typed_.empty()) {
        for (const view::Typing& kTyping : typed_) {
            take(kTyping);
        }
        typed_.clear();
        pressedSinceLayout_ = true;
    }
    // A press read in one frame with one before it is hit against what that
    // one brought, as a wheel is (D479, D486): selecting a row brings its
    // components, which the next press may be on.
    if (pressedSinceLayout_) {
        const double kSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began_).count() + 1;
        static_cast<void>(tree_->layOut(root_, static_cast<float>(width_), static_cast<float>(height_), kSeconds));
    }
    pressedSinceLayout_ = true;
    // Move to waits for this press alone (D498).
    return std::exchange(moving_, false);
}

void ShellParticipant::pressOn(ui::Node node, bool moving) {
    // A graph's node on the canvas stands for its row (D819), as does a
    // blockout's footprint, which a drag then moves (D852), and a mark on a
    // clip's timeline, moved in time by a drag (D890).
    held_.planPressed = false;
    if (const std::optional<std::size_t> kStands = canvasEntityOf(node);
        kStands.has_value() && *kStands < entityRows_.size()) {
        node = entityRows_[*kStands];
        held_.planPressed = chosen(Listed::Blockout) || held_.timelineScale > 0;
    }
    // An output pressed waits for the input to connect (D733).
    const std::optional<std::pair<std::string, std::string>> kConnecting = std::exchange(connecting_, std::nullopt);
    // The start screen's own presses (D781).
    if (!gameOpen()) {
        pressStart(node);
        return;
    }
    // The Preferences and Export dialogs' own (D782, D872).
    if (pressPreferences(node) || pressExport(node)) {
        return;
    }
    // The left dock's tabs and the Layers' rows (D788), a terrain's map
    // (D856), and a tilemap's grid (D861).
    if (pressLayers(node) || pressBrowser(node) || pressMap(node) || pressTiles(node)) {
        return;
    }
    // A document's tab is pressed as its row in Assets is (D776).
    if (const auto kTab = std::ranges::find(tabRows_, node); kTab != tabRows_.end()) {
        const auto kListed =
            std::ranges::find(scenes_, openDocuments_[static_cast<std::size_t>(kTab - tabRows_.begin())]);
        const auto kAt = static_cast<std::size_t>(kListed - scenes_.begin());
        // Its row may be left out of the browser's list (D799).
        if (kAt < sceneRows_.size() && sceneRows_[kAt] == ui::Node{}) {
            showScene(kAt);
            return;
        }
        if (kAt < sceneRows_.size()) {
            node = sceneRows_[kAt];
        }
    }
    // A field's value chosen from its list (D784).
    if (const auto kChoice = std::ranges::find(choiceRows_, node, &std::pair<ui::Node, std::string>::first);
        kChoice != choiceRows_.end() && choosing_.has_value()) {
        const FieldRow kChosenField = *choosing_;
        const std::string kName = kChoice->second;
        closePopup();
        endEdit();
        apply(kChosenField, kName);
        return;
    }
    // A menu's or the matches' row does its command, a menu's button opens
    // its menu, the palette's field takes the keyboard, and any other press
    // closes what is open (D777).
    if (const auto kLine = std::ranges::find(popupRows_, node, &std::pair<ui::Node, std::size_t>::first);
        kLine != popupRows_.end()) {
        const std::string kName = commands_[kLine->second].name;
        closePalette();
        closePopup();
        static_cast<void>(runCommand(kName));
        return;
    }
    if (const auto kMenu = std::ranges::find(menus_, node, &std::pair<ui::Node, std::string>::first);
        kMenu != menus_.end()) {
        showMenu(static_cast<std::size_t>(kMenu - menus_.begin()));
        return;
    }
    closePopup();
    if (palette_.has_value() && node == palette_->value) {
        openPalette();
        return;
    }
    closePalette();
    // A splitter held follows the pointer till it is let go (D779).
    // From where it was pressed, not where the pointer is by now.
    if (const auto kEdge = std::ranges::find(splitters_, node); kEdge != splitters_.end()) {
        const auto kWhich = static_cast<Edge>(kEdge - splitters_.begin());
        const std::array<float, 2> kAt = pressedAt_;
        const float kSize = kWhich == Edge::Hierarchy     ? preferences_.hierarchyWidth
                            : kWhich == Edge::Inspector   ? preferences_.inspectorWidth
                            : preferences_.dockHeight > 0 ? preferences_.dockHeight
                                                          : tree_->rectOf(bottomDock_).height;
        resizing_ = Resizing{.edge = kWhich, .from = kWhich == Edge::Dock ? kAt[1] : kAt[0], .size = kSize};
        released_ = false;
        return;
    }
    // The row is the node pressed or the one its words are on.
    // At a host joined the cook runs on the host's machine, with its tool
    // (D885).
    if ((!cookTool_.empty() || held_.joined != nullptr) && node == cookNode_) {
        endEdit();
        cookOrStop();
    } else if (const auto kMode = std::ranges::find(held_.modeButtons, node, &std::pair<ui::Node, std::string>::first);
               kMode != held_.modeButtons.end()) {
        // A viewport mode chosen in the palette (D910).
        endEdit();
        const std::string kChosenMode = kMode->second;
        chooseMode(kChosenMode);
    } else if (play_.has_value() && node == playNode_) {
        endEdit();
        if (playing_.has_value()) {
            stopPlaying();
        } else {
            startPlaying();
        }
    } else if (broken_.has_value() && node == openNode_) {
        openEditor(*diagnostic_);
    } else if (broken_.has_value() && node == retryNode_) {
        // Read again; shown whole as it now reads, or still as it does not.
        const bool kOpened = openGame();
        rebuild();
        if (kOpened) {
            say("the game reads");
        }
    } else if (broken_.has_value()) {
        return;
    } else if (node == assetsTab_ || node == historyTab_ || node == consoleTab_ || node == networkTab_ ||
               node == environmentTab_ || node == profilerTab_ || node == held_.frameTab || node == held_.changesTab ||
               node == held_.panelsTab) {
        showDock(node == assetsTab_         ? Dock::Assets
                 : node == historyTab_      ? Dock::History
                 : node == consoleTab_      ? Dock::Console
                 : node == networkTab_      ? Dock::Network
                 : node == environmentTab_  ? Dock::Environment
                 : node == profilerTab_     ? Dock::Profiler
                 : node == held_.frameTab   ? Dock::Frame
                 : node == held_.changesTab ? Dock::Changes
                                            : Dock::Panels);
    } else if (pressNetwork(node) || pressEnvironment(node) || pressCapture(node) || pressChanges(node)) {
        return;
    } else if (const auto kFilter = std::ranges::find(consoleFilters_, node, &std::pair<ui::Node, int>::first);
               kFilter != consoleFilters_.end()) {
        // A source's button shows or hides its lines; the last shows
        // warnings and errors alone, or everything (D778).
        if (kFilter->second < 3) {
            consoleSources_[static_cast<std::size_t>(kFilter->second)] =
                !consoleSources_[static_cast<std::size_t>(kFilter->second)];
        } else {
            consoleProblems_ = !consoleProblems_;
        }
        showConsole();
    } else if (const auto kLogged = std::ranges::find(consoleRows_, node, &std::pair<ui::Node, std::size_t>::first);
               kLogged != consoleRows_.end()) {
        // A line naming a place in the game's own files opens it there.
        if (kLogged->second < console_.size() && console_[kLogged->second].at.has_value() && !editor_.empty()) {
            const Diagnostic kAt = *console_[kLogged->second].at;
            openEditor(kAt);
        }
    } else if (node == undoNode_ || node == redoNode_) {
        endEdit();
        step(node == undoNode_ ? "authoring.undo" : "authoring.redo");
    } else if (chosen(Listed::Sheet) && node == newNode_) {
        // A sheet's class is made and deleted as an entity is (D638).
        endEdit();
        const std::string kStyle = mintedHexIdentity();
        Value operation = Value::object();
        operation.add("operation", Value::string("styles.create_class"));
        operation.add("style", Value::string(kStyle));
        // Named as a class's name may be, from its identity, till renamed.
        operation.add("name", Value::string("class_" + kStyle.substr(0, 6)));
        commit(std::move(operation), "class made", kStyle);
    } else if (chosen(Listed::Sheet) && node == deleteNode_ && !entity_.empty()) {
        endEdit();
        Value operation = Value::object();
        operation.add("operation", Value::string("styles.remove_class"));
        operation.add("style", Value::string(entity_));
        commit(std::move(operation), "class deleted", std::nullopt);
    } else if (chosen(Listed::Ui) && node == newNode_) {
        // Of a component the game declares a node, not one the document
        // holds (D672).
        showNodeForm();
    } else if (chosen(Listed::Graph) && node == newNode_) {
        // Of a type the material module lists, with a fresh identity
        // (D731).
        showGraphForm();
    } else if (chosen(Listed::Graph) && node == deleteNode_ && !entity_.empty()) {
        // A node another reads is refused by the session: the graph would
        // not read (D728).
        endEdit();
        Value operation = Value::object();
        operation.add("operation", Value::string("materials.remove_node"));
        operation.add("node", Value::string(entity_));
        commit(std::move(operation), "node deleted", std::nullopt);
    } else if (chosen(Listed::Ui) && node == deleteNode_ && !entity_.empty()) {
        // A node is removed with what is inside it (D643).
        endEdit();
        Value operation = Value::object();
        operation.add("operation", Value::string("ui.remove_node"));
        operation.add("node", Value::string(entity_));
        commit(std::move(operation), "node deleted", std::nullopt);
    } else if (chosen(Listed::Ui) && (node == upNode_ || node == downNode_) && !entity_.empty()) {
        endEdit();
        moveNode(node == upNode_ ? -1 : 1);
    } else if (chosen(Listed::Ui) && node == moveNode_ && !entity_.empty()) {
        // Into the node pressed next, or to the document's top (D671).
        endEdit();
        moving_ = true;
        say("choose the node to move it into, or the document for its top");
    } else if (documentChosen() &&
               (node == newNode_ || node == deleteNode_ || node == duplicateNode_ || node == moveNode_ ||
                node == upNode_ || node == downNode_ || node == restoreNode_ || node == uninstanceNode_)) {
        // The rest are an entity's (D637).
        endEdit();
        ++refused_;
        say(chosen(Listed::Sheet)   ? "not done to a style class"
            : chosen(Listed::Ui)    ? "not done to a UI node"
            : chosen(Listed::Graph) ? "not done to a material's node"
                                    : "not done to a material's input");
    } else if (node == newNode_ && !scene_.empty()) {
        endEdit();
        create();
    } else if (node == deleteNode_ && !entity_.empty() && selected_.size() > 1) {
        // Every entity chosen, as one step (D787).
        endEdit();
        std::vector<Value> operations;
        for (const std::string& kEach : selected_) {
            Value destroyed = Value::object();
            destroyed.add("operation", Value::string("scene.destroy_entity"));
            destroyed.add("entity", Value::string(kEach));
            operations.push_back(std::move(destroyed));
        }
        const std::size_t kCount = selected_.size();
        selected_.clear();
        ++together_;
        commitAll(std::move(operations), std::to_string(kCount) + " entities deleted", std::nullopt);
    } else if (node == deleteNode_ && !entity_.empty()) {
        endEdit();
        // An entity an instance brought is removed through its patch, so
        // Restore takes it back (D154, D450).
        const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
        commit(operationOn("scene.destroy_entity"),
               kBrought ? "removed from its instance" : "entity deleted",
               kBrought ? std::optional<std::string>{entity_} : std::nullopt);
    } else if (node == duplicateNode_ && !entity_.empty()) {
        endEdit();
        duplicate();
    } else if (node == moveNode_ && !entity_.empty()) {
        endEdit();
        moving_ = true;
        say("choose the scene to move it to");
    } else if ((node == upNode_ || node == downNode_) && !entity_.empty()) {
        endEdit();
        move(node == upNode_ ? -1 : 1);
    } else if ((node == restoreNode_ || node == uninstanceNode_) && !entity_.empty()) {
        endEdit();
        const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
        const bool kRemoved = entityAt_ < removed_.size() && removed_[entityAt_];
        if (!kBrought || (node == restoreNode_ && !kRemoved)) {
            ++refused_;
            say(kBrought ? "its instance has not removed it" : "no instance brought it");
        } else if (node == restoreNode_) {
            commit(operationOn("scene.restore_entity"), "restored", entity_);
        } else {
            commit(operationOn("scene.remove_instance"), "instance removed", std::nullopt);
        }
    } else if (const auto kAction = std::ranges::find(actions_, node, &ActionButton::node); kAction != actions_.end()) {
        endEdit();
        const ActionButton kAsked = *kAction;
        if (kAsked.connects) {
            connecting_ = std::pair{entity_, kAsked.field};
            say("choose the input to connect " + kAsked.field + " to");
            return;
        }
        if (kAsked.makesNode && chosen(Listed::Graph)) {
            const std::string kNode = mintedHexIdentity();
            Value operation = Value::object();
            operation.add("operation", Value::string("materials.create_node"));
            operation.add("node", Value::string(kNode));
            operation.add("type", Value::string(kAsked.component));
            commit(std::move(operation), kAsked.done, kNode);
            return;
        }
        if (kAsked.makesNode) {
            Value operation = Value::object();
            operation.add("operation", Value::string("ui.create_node"));
            operation.add("node", Value::string(kAsked.component));
            commit(std::move(operation), kAsked.done, kAsked.component);
            return;
        }
        Value operation = Value::object();
        if (kAsked.whole) {
            operation.add("operation", Value::string(kAsked.operation));
        } else {
            operation = operationOn(kAsked.operation);
        }
        operation.add("component", Value::string(kAsked.component));
        if (!kAsked.field.empty()) {
            operation.add("field", Value::string(kAsked.field));
        }
        commit(std::move(operation), kAsked.done, entity_);
    } else if (const auto kEntry = std::ranges::find(entryRows_, node); kEntry != entryRows_.end()) {
        endEdit();
        stepTo(static_cast<std::size_t>(kEntry - entryRows_.begin()));
    } else if (const auto kScene = std::ranges::find(sceneRows_, node); kScene != sceneRows_.end()) {
        if (moving && chosen(Listed::Ui) && static_cast<std::size_t>(kScene - sceneRows_.begin()) == sceneAt_) {
            moveNodeInto(std::nullopt);
        } else if (moving && chosen(Listed::Ui)) {
            ++refused_;
            say("a node moves within its document");
        } else if (moving) {
            moveTo(static_cast<std::size_t>(kScene - sceneRows_.begin()));
        } else {
            showScene(static_cast<std::size_t>(kScene - sceneRows_.begin()));
        }
    } else if (pressLive(node)) {
        return;
    } else if (const auto kEntity = std::ranges::find(entityRows_, node); kEntity != entityRows_.end()) {
        // Held, it may be dragged onto another (D785).
        if (pointerPress_) {
            heldEntity_ = static_cast<std::size_t>(kEntity - entityRows_.begin());
        }
        if (moving && chosen(Listed::Ui)) {
            moveNodeInto(entities_[static_cast<std::size_t>(kEntity - entityRows_.begin())]);
        } else if (!documentChosen()) {
            // A scene's entities are chosen one or several at a time (D787).
            chooseEntity(static_cast<std::size_t>(kEntity - entityRows_.begin()));
        } else {
            showEntity(static_cast<std::size_t>(kEntity - entityRows_.begin()));
            // Its input is pressed next.
            connecting_ = chosen(Listed::Graph) ? kConnecting : std::nullopt;
        }
    } else if (const auto kFieldAt = std::ranges::find(fields_, node, &FieldRow::value); kFieldAt != fields_.end()) {
        if (kConnecting.has_value() && kFieldAt->role == FieldRow::Role::GraphInput) {
            endEdit();
            connectGraphInput(*kFieldAt, *kConnecting);
        } else if (const std::vector<std::string> kChoices = choicesOf(kFieldAt->type).empty()
                                                                 ? assetChoices(kFieldAt->type, assets_)
                                                                 : choicesOf(kFieldAt->type);
                   kFieldAt->role == FieldRow::Role::Field && !kChoices.empty()) {
            // A field of a few values is chosen from its list, or typed
            // still, as a name always was (D455, D784).
            beginEdit(*kFieldAt);
            showChoices(*kFieldAt, kChoices);
        } else {
            beginEdit(*kFieldAt);
        }
    } else if (const auto kViewAt = std::ranges::find(viewFields_, node, &FieldRow::value);
               kViewAt != viewFields_.end()) {
        beginEdit(*kViewAt);
    } else if (newScene_.has_value() && node == newScene_->value) {
        beginEdit(*newScene_);
    } else if (findField_.has_value() && node == findField_->value) {
        beginEdit(*findField_);
    } else if (const auto kSceneAt = std::ranges::find(sceneFields_, node, &FieldRow::value);
               kSceneAt != sceneFields_.end()) {
        beginEdit(*kSceneAt);
    } else {
        endEdit();
    }
}

void ShellParticipant::beginEdit(const FieldRow& row) {
    endEdit();
    editing_ = row;
    edit_ = std::make_unique<ui::TextEdit>(
        *tree_, row.value, ui::EditSettings{.caretColor = kText, .selectionColor = 0x4A6FA5AAU});
    static_cast<void>(edit_->press(ui::EditKey::SelectAll, {}));
    static_cast<void>(tree_->setLook(row.value, ui::Look{.fill = kEditing, .radius = 3}));
    if (typing_ != nullptr) {
        const auto kPlace = tree_->placeOf(root_, row.value);
        typing_->focus(kPlace.has_value() ? view::UiTyping::Caret{kPlace->x, kPlace->y, kPlace->width, kPlace->height}
                                          : view::UiTyping::Caret{});
    }
}

void ShellParticipant::endEdit() {
    if (edit_ == nullptr) {
        return;
    }
    edit_.reset();
    if (editing_.has_value() && tree_->contains(editing_->value)) {
        static_cast<void>(tree_->setLook(editing_->value, ui::Look{.fill = kField, .radius = 3}));
    }
    editing_.reset();
    if (typing_ != nullptr) {
        typing_->focus(std::nullopt);
    }
}

void ShellParticipant::take(const view::Typing& typing) {
    // A shortcut does its command whether or not a field holds the
    // keyboard (D777).
    if (typing.kind == view::Typing::Kind::Chord) {
        runChord(typing.text);
        return;
    }
    if (edit_ == nullptr || !editing_.has_value()) {
        return;
    }
    const std::optional<view::TypingKey> kLeft = view::edit(*edit_, typing);
    if (editing_->role == FieldRow::Role::Palette) {
        paletteTyped(kLeft);
        return;
    }
    // Typing given or dismissed closes the list it was beside (D784).
    if ((kLeft == view::TypingKey::Submit || kLeft == view::TypingKey::Dismiss) && choosing_.has_value()) {
        closePopup();
    }
    if (kLeft == view::TypingKey::Submit) {
        const FieldRow kEdited = *editing_;
        const std::string kTyped{tree_->textOf(kEdited.value)};
        endEdit();
        if (kEdited.role == FieldRow::Role::View) {
            setView(kEdited.field, kTyped);
        } else if (kEdited.role == FieldRow::Role::Instance) {
            place(kTyped);
        } else if (kEdited.role == FieldRow::Role::NewScene) {
            makeScene(kTyped);
        } else if (kEdited.role == FieldRow::Role::NewGame) {
            makeGame(kTyped);
        } else if (kEdited.role == FieldRow::Role::Shortcut) {
            setKeys(kEdited.field, kTyped);
        } else if (kEdited.role == FieldRow::Role::Editor) {
            setEditor(kTyped);
        } else if (kEdited.role == FieldRow::Role::CommitMessage) {
            commitChanges(kTyped);
        } else if (kEdited.role == FieldRow::Role::ExportSetting) {
            setExport(kEdited.field, kTyped);
        } else if (kEdited.role == FieldRow::Role::OpenGame) {
            chooseGame(std::filesystem::path{kTyped});
        } else if (kEdited.role == FieldRow::Role::Find) {
            find_ = kTyped;
            refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
        } else {
            apply(kEdited, kTyped);
        }
    } else if (kLeft == view::TypingKey::Dismiss) {
        const FieldRow::Role kRole = editing_->role;
        endEdit();
        if (kRole == FieldRow::Role::NewGame || kRole == FieldRow::Role::OpenGame ||
            kRole == FieldRow::Role::Shortcut || kRole == FieldRow::Role::Editor ||
            kRole == FieldRow::Role::CommitMessage || kRole == FieldRow::Role::ExportSetting) {
            return;
        }
        if (kRole == FieldRow::Role::View) {
            showViewText();
        } else if (kRole == FieldRow::Role::NewScene) {
            static_cast<void>(showScenes());
        } else if (kRole == FieldRow::Role::Instance || kRole == FieldRow::Role::Find) {
            refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
        } else {
            showEntity(entityAt_);
        }
    }
}

Value ShellParticipant::operationOn(std::string_view operation) const {
    Value made = Value::object();
    made.add("operation", Value::string(std::string{operation}));
    made.add("entity", Value::string(entity_));
    return made;
}

void ShellParticipant::apply(const FieldRow& row, const std::string& text) {
    if (row.role == FieldRow::Role::Part) {
        setPart(row, text);
        return;
    }
    if (row.role == FieldRow::Role::NodeField) {
        setNodeField(row, text);
        return;
    }
    if (row.role == FieldRow::Role::Override) {
        setOverride(text);
        return;
    }
    if (row.role == FieldRow::Role::ImportSetting) {
        setImportSetting(row, text);
        return;
    }
    if (row.role == FieldRow::Role::DataValue) {
        setDataValue(row, text);
        return;
    }
    if (row.role == FieldRow::Role::SettingLine) {
        setSettingLine(row, text);
        return;
    }
    if (row.role == FieldRow::Role::NewSetting) {
        putSettingLine(row, text);
        return;
    }
    if (row.role == FieldRow::Role::SolidWord || row.role == FieldRow::Role::NewSolid) {
        setSolid(row, text);
        return;
    }
    if (row.role == FieldRow::Role::BrushSetting) {
        setBrush(row, text);
        return;
    }
    if (row.role == FieldRow::Role::TileSetting) {
        setTilePaint(text);
        return;
    }
    if (row.role == FieldRow::Role::GraphParam || row.role == FieldRow::Role::GraphInput) {
        setGraphMember(row, text);
        return;
    }
    // An entity on a locked layer is not edited (D788).
    if (row.role == FieldRow::Role::Field || row.role == FieldRow::Role::Name) {
        for (const std::string& kEach : selected_.empty() ? std::vector<std::string>{entity_} : selected_) {
            if (const std::optional<std::string> kLayer = lockedOf(kEach); kLayer.has_value()) {
                ++lockRefused_;
                say(*kLayer + " is locked");
                refresh(entity_);
                return;
            }
        }
    }
    if (row.role == FieldRow::Role::Name && chosen(Listed::Sheet)) {
        Value operation = Value::object();
        operation.add("operation", Value::string("styles.rename_class"));
        operation.add("style", Value::string(entity_));
        operation.add("name", Value::string(text));
        commit(std::move(operation), "renamed " + text, entity_);
        return;
    }
    Value operation;
    std::string done;
    if (row.role == FieldRow::Role::Name) {
        operation = operationOn("scene.rename_entity");
        operation.add("name", Value::string(text));
        done = "renamed " + text;
    } else if (row.role == FieldRow::Role::Add) {
        std::string why;
        const std::optional<Catalog::Component> kAdded = componentNamed(catalog_, text, why);
        if (!kAdded.has_value()) {
            refuse(why);
            return;
        }
        operation = operationOn("scene.add_component");
        operation.add("component", Value::string(kAdded->id));
        done = kAdded->name + " added";
    } else if (row.kind == "reference" || row.kind == "entity") {
        // A reference, unset (its type's kind) or set (its value's), names
        // an entity of the scene, or none when cleared.
        operation = operationOn("scene.set_reference");
        operation.add("component", Value::string(row.component));
        operation.add("field", Value::string(row.field));
        if (text.empty()) {
            operation.add("target", Value{});
            done = row.field + " cleared";
        } else {
            std::string why;
            const std::optional<std::string> kTarget = entityNamed(entities_, names_, text, why);
            if (!kTarget.has_value()) {
                refuse(row.field + ": " + why);
                return;
            }
            operation.add("target", Value::string(*kTarget));
            done = row.field + " set to " + text;
        }
    } else {
        // A label or a color typed as the number it stands for (D784).
        const std::string kTyped = typedAs(row.type, text);
        std::optional<Value> kValue = typedValue(row.kind, kTyped);
        // An unsigned field takes an asset by its name, as its identity.
        std::string why;
        const std::optional<Asset> kAsset =
            !kValue.has_value() && row.kind == "unsigned" ? assetNamed(assets_, text, why) : std::nullopt;
        if (kAsset.has_value()) {
            kValue = typedValue("unsigned", std::to_string(kAsset->id));
        }
        if (!kValue.has_value()) {
            refuse(row.field + ": " +
                   (row.kind.empty() ? std::string{"not edited here"}
                    : row.kind == "unsigned" && !why.empty() && why.rfind("no asset", 0) != 0 ? why
                                                                                              : "not a " + row.kind));
            return;
        }
        operation = operationOn("scene.set_field");
        operation.add("component", Value::string(row.component));
        operation.add("field", Value::string(row.field));
        operation.add("value", *kValue);
        done = row.field + " set to " + text;
        // Past its range is said, not stopped (D783).
        if (pastRange(row.type, kTyped)) {
            done += ", past " + hintText(row.type);
        }
    }
    // A field set or a reference given is done to every entity chosen
    // (D787); the rest is the one entity's.
    if (row.role == FieldRow::Role::Field) {
        commitChosen(std::move(operation), done);
        return;
    }
    commit(std::move(operation), done, entity_);
}

void ShellParticipant::create() {
    const std::string kIdentity = mintedIdentity();
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.create_entity"));
    operation.add("entity", Value::string(kIdentity));
    operation.add("name", Value::string("new entity"));
    commit(std::move(operation), "entity created", kIdentity);
}

void ShellParticipant::openEditor(const Diagnostic& at) {
    // Only the game's own files: a place a log names may point anywhere.
    const std::optional<std::filesystem::path> kFile = gameFile(description_.parent_path(), at.file);
    if (!kFile.has_value()) {
        say(at.file + " is not one of the game's files");
        return;
    }
    const std::vector<std::string> kWords = editorCommand(editor_, kFile->string(), at.line, at.column);
    if (kWords.empty()) {
        return;
    }
    const char* const kPath = std::getenv("PATH");
    const auto kProgram = programOnPath(kWords.front(), kPath != nullptr ? kPath : "");
    if (!kProgram.has_value()) {
        say("no editor " + kWords.front() + " is on the path; studio.editor names one");
        return;
    }
    const std::vector<std::string> kArguments(kWords.begin() + 1, kWords.end());
    // A batch file's arguments are parsed again by cmd.exe: a file name
    // holding what it acts on would run as a command (D453a).
    if (isBatch(*kProgram) && !std::ranges::all_of(kArguments, batchSafe)) {
        say("the editor is a batch file and the file's path holds characters it would run; name the editor's own "
            "program in studio.editor");
        return;
    }
    auto started = process::Child::start({.program = *kProgram, .arguments = kArguments});
    if (!started.has_value()) {
        say("the editor did not start: " + std::string{started.error().description()});
        return;
    }
    // Kept until Studio ends, which ends what is still running: an editor's
    // launcher, as VS Code's `code`, hands the file over and returns.
    editors_.push_back(std::move(*started));
    ++opened_;
    say("opened " + diagnostic_->file.substr(5) + " at line " + std::to_string(diagnostic_->line));
}

void ShellParticipant::duplicate() {
    const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    if (kBrought || components == nullptr || components->kind() != Value::Kind::Array) {
        ++refused_;
        say(kBrought ? "an instance's entity is its source's to copy" : "nothing to copy");
        return;
    }
    const std::string kCopy = mintedIdentity();
    const std::string kName = (entityAt_ < names_.size() ? names_[entityAt_] : std::string{}) + " copy";
    commitAll(copyOf(*components, kCopy, kName), kName + " made", kCopy);
}

std::vector<Value>
ShellParticipant::copyOf(const Value& components, const std::string& entity, const std::string& called) {
    std::vector<Value> operations;
    Value made = Value::object();
    made.add("operation", Value::string("scene.create_entity"));
    made.add("entity", Value::string(entity));
    made.add("name", Value::string(called));
    operations.push_back(std::move(made));
    for (const Value& each : components.items()) {
        const Value* component = each.find("component");
        if (component == nullptr || component->text() == nullptr) {
            continue;
        }
        Value added = Value::object();
        added.add("operation", Value::string("scene.add_component"));
        added.add("entity", Value::string(entity));
        added.add("component", *component);
        operations.push_back(std::move(added));
        // Each value the scene gives, as it reads: a reference names its
        // target again, and every other field takes its value. A value only
        // recorded, its field no longer the catalog's, is left behind, as a
        // component the catalog no longer knows is above.
        const Value* fields = each.find("fields");
        for (const Value& field :
             fields != nullptr && fields->kind() == Value::Kind::Array ? fields->items() : std::span<const Value>{}) {
            const Value* name = field.find("name");
            const Value* value = field.find("value");
            if (name == nullptr || name->text() == nullptr || value == nullptr ||
                value->kind() != Value::Kind::Object || value->names().size() != 1) {
                continue;
            }
            const std::string& kKind = value->names().front();
            if (kKind == "recorded") {
                continue;
            }
            Value set = Value::object();
            set.add("entity", Value::string(entity));
            set.add("component", *component);
            set.add("field", *name);
            if (kKind == "entity") {
                set.add("operation", Value::string("scene.set_reference"));
                set.add("target", *value->find("entity"));
            } else {
                set.add("operation", Value::string("scene.set_field"));
                set.add("value", *value);
            }
            operations.push_back(std::move(set));
        }
    }
    return operations;
}

void ShellParticipant::moveTo(std::size_t sceneAt) {
    if (sceneAt < listed_.size() && listed_[sceneAt] != Listed::Scene) {
        ++refused_;
        say("an entity moves to a scene");
        return;
    }
    const std::string kTarget = sceneAt < scenes_.size() ? scenes_[sceneAt] : std::string{};
    const bool kBrought = entityAt_ < brought_.size() && brought_[entityAt_];
    const std::optional<Value> kRead = read(scene_, "scene.read_entity", entity_);
    const Value* components = kRead.has_value() ? kRead->find("components") : nullptr;
    if (kTarget == scene_ || kBrought || components == nullptr || components->kind() != Value::Kind::Array) {
        ++refused_;
        say(kTarget == scene_ ? "already in that scene"
            : kBrought        ? "an instance's entity is its source's to move"
                              : "nothing to move");
        return;
    }
    // Its own id kept: made in the other scene and deleted from this one,
    // both or neither (D497). A reference it holds to an entity left here
    // names nothing there, and refuses the whole.
    const std::string kName = entityAt_ < names_.size() ? names_[entityAt_] : std::string{};
    std::vector<Value> gone;
    gone.push_back(operationOn("scene.destroy_entity"));
    std::vector<std::pair<std::string, std::vector<Value>>> parts;
    parts.emplace_back(scene_, std::move(gone));
    parts.emplace_back(kTarget, copyOf(*components, entity_, kName));
    const Outcome kOutcome = outcomeOf(ask(togetherRecord(next(), std::move(parts))));
    told(kOutcome);
    if (kOutcome.done) {
        ++applied_;
        say(kName + " moved to " + kTarget);
    } else {
        ++refused_;
        say(kOutcome.message);
    }
    refresh(std::nullopt);
}

void ShellParticipant::move(int by) {
    const std::optional<std::int64_t> kPlace = entityAt_ < places_.size() ? places_[entityAt_] : std::nullopt;
    const auto kOwn = std::ranges::count_if(places_, [](const auto& place) {
        return place.has_value();
    });
    if (!kPlace.has_value() || *kPlace + by < 0 || *kPlace + by >= kOwn) {
        ++refused_;
        say(!kPlace.has_value() ? "an instance places what it brings" : by < 0 ? "already first" : "already last");
        return;
    }
    Value operation = operationOn("scene.move_entity");
    operation.add("place", Value::integer(*kPlace + by));
    commit(std::move(operation), by < 0 ? "moved up" : "moved down", entity_);
}

void ShellParticipant::place(const std::string& text) {
    std::string why;
    const std::optional<std::size_t> kScene = sceneNamed(scenes_, text, why);
    if (!kScene.has_value()) {
        ++refused_;
        say(why);
        refresh(entity_.empty() ? std::nullopt : std::optional<std::string>{entity_});
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("scene.add_instance"));
    operation.add("scene", Value::string(sceneSources_[*kScene]));
    operation.add("instance", Value::string(mintedIdentity()));
    commit(std::move(operation), "instance of " + scenes_[*kScene] + " placed", std::nullopt);
}

void ShellParticipant::makeScene(std::string text) {
    // A file named by its absolute path is imported, beside the game's own
    // files under its own name (D505).
    if (std::filesystem::path{text}.is_absolute()) {
        static_cast<void>(importFile(text, std::filesystem::path{text}.filename().generic_string()));
        static_cast<void>(showScenes());
        return;
    }
    if (!text.empty() && !text.ends_with(".scene")) {
        text += ".scene";
    }
    const Answered kMade = answeredOf(ask(createSceneRecord(next(), text)));
    if (!kMade.done || text.empty()) {
        ++refused_;
        say(kMade.done ? "name the new scene" : kMade.message);
        static_cast<void>(showScenes());
        return;
    }
    ++applied_;
    const auto kAt = std::ranges::upper_bound(scenes_, text);
    const auto kIndex = static_cast<std::size_t>(kAt - scenes_.begin());
    scenes_.insert(kAt, text);
    sceneSources_.insert(sceneSources_.begin() + static_cast<std::ptrdiff_t>(kIndex), kMade.resource);
    // The chosen scene's row moves down when the new one sorts before it.
    if (!scene_.empty() && kIndex <= sceneAt_) {
        ++sceneAt_;
    }
    static_cast<void>(showScenes());
    showScene(kIndex);
    say(text + " made");
}

void ShellParticipant::refuse(const std::string& why) {
    ++refused_;
    say(why);
    showEntity(entityAt_);
}

void ShellParticipant::commit(Value operation, const std::string& done, const std::optional<std::string>& choose) {
    std::vector<Value> operations;
    operations.push_back(std::move(operation));
    commitAll(std::move(operations), done, choose);
}

void ShellParticipant::commitAll(std::vector<Value> operations,
                                 const std::string& done,
                                 const std::optional<std::string>& choose) {
    const Outcome kOutcome = outcomeOf(ask(applyRecord(next(), scene_, std::move(operations))));
    told(kOutcome);
    if (kOutcome.done) {
        ++applied_;
        say(done);
        recookPlayed();
    } else {
        ++refused_;
        say(kOutcome.message);
    }
    refresh(choose);
}

void ShellParticipant::refresh(const std::optional<std::string>& entity) {
    const std::array<float, 2> kEntities = tree_->scrollOf(entitiesColumn_);
    const std::array<float, 2> kComponents = tree_->scrollOf(componentsColumn_);
    const std::vector<std::string> kChosenBefore = selected_;
    showScene(sceneAt_);
    if (entity.has_value()) {
        if (const auto kAt = std::ranges::find(entities_, *entity); kAt != entities_.end()) {
            showEntity(static_cast<std::size_t>(kAt - entities_.begin()));
            // Several chosen stay chosen while they are all still there.
            if (kChosenBefore.size() > 1 && std::ranges::all_of(kChosenBefore, [&](const std::string& each) {
                    return std::ranges::contains(entities_, each);
                })) {
                selected_ = kChosenBefore;
                showSelection(static_cast<std::size_t>(kAt - entities_.begin()));
            }
        }
    }
    static_cast<void>(tree_->scrollTo(entitiesColumn_, kEntities[0], kEntities[1]));
    static_cast<void>(tree_->scrollTo(componentsColumn_, kComponents[0], kComponents[1]));
    showHistoryList();
}

void ShellParticipant::stepTo(std::size_t entry) {
    const auto kTarget = static_cast<std::int64_t>(entry) + 1;
    // Each step is one record, as Undo and Redo are; a step the session
    // refuses leaves the counts as they were and ends the walk.
    while (undoable_ != kTarget) {
        const std::int64_t kBefore = undoable_;
        step(undoable_ > kTarget ? "authoring.undo" : "authoring.redo");
        if (undoable_ == kBefore) {
            break;
        }
    }
}

void ShellParticipant::step(std::string_view kind) {
    if (scene_.empty()) {
        return;
    }
    const Outcome kOutcome = outcomeOf(ask(stepRecord(next(), kind, scene_)));
    told(kOutcome);
    const bool kUndo = kind == "authoring.undo";
    if (kOutcome.done) {
        ++(kUndo ? undone_ : redone_);
        say(kUndo ? "undone" : "redone");
        recookPlayed();
    } else {
        say(kOutcome.message);
    }
    refresh(entity_.empty() ? std::nullopt : std::optional{entity_});
}

void ShellParticipant::told(const Outcome& outcome) {
    undoable_ = outcome.undoable;
    redoable_ = outcome.redoable;
    static_cast<void>(showHistory());
    if (outcome.view.has_value()) {
        view_ = outcome.view;
        showViewText();
    }
}

} // namespace rawframe::studio
