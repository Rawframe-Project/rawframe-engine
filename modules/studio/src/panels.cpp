#include "hierarchy.h"
#include "hints.h"
#include "shell.h"
#include "surfaces.h"

#include <algorithm>

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

ui::Layout ShellParticipant::columnLayout(float width) {
    return ui::Layout{.width = ui::pixels(width),
                      .direction = ui::Direction::Column,
                      .gap = 4,
                      .grow = width > 0 ? 0.0F : 1.0F,
                      .shrink = width > 0 ? 0.0F : 1.0F,
                      .padding = {8, 8, 8, 8},
                      .scroll = ui::Scroll::Vertical};
}

ui::Layout ShellParticipant::leftLayout(float width) {
    return ui::Layout{.width = ui::pixels(width),
                      .direction = ui::Direction::Column,
                      .grow = width > 0 ? 0.0F : 1.0F,
                      .shrink = width > 0 ? 0.0F : 1.0F};
}

ui::Layout ShellParticipant::dockLayout(float height) {
    return ui::Layout{.height = ui::pixels(height),
                      .minHeight = ui::pixels(0),
                      .direction = ui::Direction::Column,
                      .gap = 4,
                      .grow = height > 0 ? 0.0F : 2.0F,
                      .shrink = height > 0 ? 0.0F : 1.0F,
                      .padding = {8, 8, 8, 8}};
}

result::Result<ui::Node> ShellParticipant::column(ui::Node parent, std::string_view heading, float width) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kColumn, box(parent, columnLayout(width), kPanel));
    // A column scrolls what does not fit (D441), so its rows keep their
    // heights rather than shrinking to fit.
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kHeading,
        box(kColumn, ui::Layout{.height = ui::pixels(22), .alignItems = ui::Align::Center, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeading, ui::Layout{}, 0));
    RAWFRAME_TRY(words(kTitle, heading, kQuiet, 13));
    // Read as a list that scrolls, by its heading (D575).
    RAWFRAME_TRY(tree_->setRole(kColumn, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(kColumn, heading));
    return kColumn;
}

result::Result<ui::Node> ShellParticipant::pane(std::string_view name) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kPane,
                        box(std::nullopt,
                            ui::Layout{.minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .grow = 1,
                                       .scroll = ui::Scroll::Vertical},
                            kPanel));
    RAWFRAME_TRY(tree_->setRole(kPane, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(kPane, name));
    return kPane;
}

result::Result<ui::Node> ShellParticipant::button(ui::Node parent, std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kButton, box(parent, ui::Layout{.padding = {10, 4, 10, 4}}, kRow));
    RAWFRAME_TRY(pressWords(kButton, text, kText, 14));
    return kButton;
}

result::Status ShellParticipant::pressWords(ui::Node node, std::string_view text, std::uint32_t color, float size) {
    RAWFRAME_TRY(words(node, text, color, size));
    // A node's text is read as its value, so a button is named by it too.
    RAWFRAME_TRY(tree_->setRole(node, ui::Role::Button));
    return tree_->setName(node, text);
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
    RAWFRAME_TRY_ASSIGN(const ui::Node kToolbar,
                        box(root_,
                            ui::Layout{.height = ui::pixels(34),
                                       .justify = ui::Justify::SpaceBetween,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0,
                                       .padding = {12, 4, 6, 4}},
                            kHeaderFill));
    // The title, then the menu bar and the palette (D777).
    RAWFRAME_TRY_ASSIGN(const ui::Node kStart, box(kToolbar, ui::Layout{.alignItems = ui::Align::Center, .gap = 4}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kStart, ui::Layout{.padding = {0, 2, 8, 2}}, 0));
    RAWFRAME_TRY(words(kTitle, title_, kText));
    // The others at a host joined, each in their color (D884).
    held_.avatars.clear();
    held_.avatarsNode = ui::Node{};
    if (held_.joined != nullptr) {
        RAWFRAME_TRY_ASSIGN(held_.avatarsNode, box(kStart, ui::Layout{.alignItems = ui::Align::Center, .gap = 4}, 0));
        showCollaborators();
    }
    // The toolbar's commands at its end: Cook before Play, so Play, Undo,
    // and Redo keep their places (D502).
    RAWFRAME_TRY_ASSIGN(const ui::Node kEnd, box(kToolbar, ui::Layout{.alignItems = ui::Align::Center, .gap = 6}, 0));
    // The start screen has no game to cook, play, or undo in (D781); a
    // host joined cooks on its own machine (D885), its cook brought here
    // to be played (D905).
    if (!gameOpen()) {
        menus_.clear();
    } else if (!cookTool_.empty() || held_.joined != nullptr) {
        RAWFRAME_TRY_ASSIGN(cookNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(pressWords(cookNode_, cooking_.has_value() ? "Stop" : "Cook", kText, 14));
    }
    if ((session_ != nullptr || held_.joined != nullptr) && play_.has_value()) {
        RAWFRAME_TRY_ASSIGN(playNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {12, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(pressWords(playNode_, "Play", kText, 14));
    }
    if (gameOpen()) {
        RAWFRAME_TRY_ASSIGN(undoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY_ASSIGN(redoNode_, box(kEnd, ui::Layout{.width = ui::pixels(64), .padding = {10, 2, 10, 2}}, kRow));
        RAWFRAME_TRY(showHistory());
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kBody,
        box(root_, ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Row, .grow = 1}, kBackground));
    // The status bar under the body: what Studio last said.
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kStatusBar,
        box(root_,
            ui::Layout{
                .height = ui::pixels(24), .alignItems = ui::Align::Center, .shrink = 0, .padding = {10, 2, 10, 2}},
            kHeaderFill));
    RAWFRAME_TRY_ASSIGN(statusNode_, box(kStatusBar, ui::Layout{}, 0));
    // Then, at its end, the game's branch, where it is under version
    // control (D871).
    RAWFRAME_TRY(box(kStatusBar, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY_ASSIGN(held_.branchNode, box(kStatusBar, ui::Layout{}, 0));
    // A menu or the matches, and the splitters, went with the tree they
    // were in.
    splitters_ = {};
    resizing_.reset();
    popup_.reset();
    menuOpen_.reset();
    popupRows_.clear();
    if (!gameOpen()) {
        return showStart(kBody);
    }
    if (broken_.has_value()) {
        RAWFRAME_TRY(showBroken(kBody));
        return showMenus(kStart);
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kMain,
        box(kBody, ui::Layout{.width = ui::pixels(0), .direction = ui::Direction::Column, .grow = 1}, kBackground));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kUpper,
        box(kMain,
            ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(0), .direction = ui::Direction::Row, .grow = 3},
            kBackground));
    // The docks at the sizes the author left them (D779), each with a
    // splitter on its inner edge in place of the gap it had.
    // The left dock holds the Hierarchy, or the Layers in its place, each
    // heading naming the other (D788), and the Layers the World (D870).
    RAWFRAME_TRY_ASSIGN(leftDock_, box(kUpper, leftLayout(preferences_.hierarchyWidth), kBackground));
    RAWFRAME_TRY_ASSIGN(
        entitiesColumn_,
        tabbedColumn(
            leftDock_, placed("Hierarchy", SurfaceKind::Panel, "Left"), "Layers", layersTab_, "Live", &live_.button));
    RAWFRAME_TRY_ASSIGN(
        layersColumn_,
        tabbedColumn(
            leftDock_, placed("Layers", SurfaceKind::Panel, "Left"), "Hierarchy", hierarchyTab_, "World", &worldTab_));
    RAWFRAME_TRY(tree_->detach(layersColumn_));
    RAWFRAME_TRY_ASSIGN(
        worldColumn_, tabbedColumn(leftDock_, placed("World", SurfaceKind::Panel, "Left"), "Hierarchy", worldBackTab_));
    RAWFRAME_TRY(tree_->detach(worldColumn_));
    leftShown_ = Left::Hierarchy;
    RAWFRAME_TRY(splitter(kUpper, Edge::Hierarchy));
    // The entities' operations above their rows, in two rows: what makes
    // and copies, then what restores and orders.
    RAWFRAME_TRY_ASSIGN(const ui::Node kMaking,
                        box(entitiesColumn_, ui::Layout{.height = ui::pixels(28), .gap = 6, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kOrdering,
                        box(entitiesColumn_, ui::Layout{.height = ui::pixels(28), .gap = 6, .shrink = 0}, 0));
    if (catalog_.offers("scene.create_entity")) {
        RAWFRAME_TRY_ASSIGN(newNode_, button(kMaking, "New"));
    }
    if (catalog_.offers("scene.destroy_entity")) {
        RAWFRAME_TRY_ASSIGN(deleteNode_, button(kMaking, "Delete"));
    }
    // The chosen entity copied whole (D458), its operations the catalog's,
    // and moved to another scene, made there and deleted here (D498).
    if (catalog_.offers("scene.create_entity") && catalog_.offers("scene.add_component") &&
        catalog_.offers("scene.set_field")) {
        RAWFRAME_TRY_ASSIGN(duplicateNode_, button(kMaking, "Duplicate"));
        if (catalog_.offers("scene.destroy_entity")) {
            RAWFRAME_TRY_ASSIGN(moveNode_, button(kMaking, "Move to"));
        }
    }
    // An instance's entity: one it removed taken back, or the whole
    // instance removed (D450).
    if (catalog_.offers("scene.restore_entity")) {
        RAWFRAME_TRY_ASSIGN(restoreNode_, button(kOrdering, "Restore"));
    }
    if (catalog_.offers("scene.remove_instance")) {
        RAWFRAME_TRY_ASSIGN(uninstanceNode_, button(kOrdering, "Remove instance"));
    }
    // The chosen entity's place among the scene's own (D451).
    if (catalog_.offers("scene.move_entity")) {
        RAWFRAME_TRY_ASSIGN(upNode_, button(kOrdering, "Up"));
        RAWFRAME_TRY_ASSIGN(downNode_, button(kOrdering, "Down"));
    }
    // The mode palette on the center's left edge (SPEC-0051, D910), its
    // buttons the new tree's.
    held_.modeButtons.clear();
    RAWFRAME_TRY_ASSIGN(held_.modePalette,
                        box(kUpper,
                            ui::Layout{.width = ui::pixels(86),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .shrink = 0,
                                       .padding = {0, 8, 0, 8}},
                            0));
    RAWFRAME_TRY(tree_->setName(held_.modePalette, "Mode palette"));
    // The center: the open documents' tabs, then the active one's view,
    // which the viewport takes over (state.md item 28 step 5).
    RAWFRAME_TRY_ASSIGN(centerColumn_,
                        box(kUpper,
                            ui::Layout{.width = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .grow = 2,
                                       .padding = {8, 8, 8, 8},
                                       .scroll = ui::Scroll::Vertical},
                            kPanel));
    RAWFRAME_TRY(tree_->setRole(centerColumn_, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(centerColumn_, "Documents"));
    RAWFRAME_TRY_ASSIGN(tabsRow_, box(centerColumn_, ui::Layout{.height = ui::pixels(28), .gap = 4, .shrink = 0}, 0));
    // The bottom dock under the Hierarchy and the center, its tabs its
    // heading.
    RAWFRAME_TRY(splitter(kMain, Edge::Dock));
    RAWFRAME_TRY_ASSIGN(bottomDock_, box(kMain, dockLayout(preferences_.dockHeight), kPanel));
    // Each panel by the name the surface table places it under (D873).
    constexpr auto kBottom = [](std::string_view name) consteval {
        return placed(name, SurfaceKind::Panel, "Bottom");
    };
    RAWFRAME_TRY_ASSIGN(const ui::Node kDockTabs,
                        box(bottomDock_, ui::Layout{.height = ui::pixels(26), .gap = 4, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(assetsTab_, button(kDockTabs, kBottom("Assets")));
    RAWFRAME_TRY_ASSIGN(historyTab_, button(kDockTabs, kBottom("History")));
    RAWFRAME_TRY_ASSIGN(consoleTab_, button(kDockTabs, kBottom("Console")));
    RAWFRAME_TRY_ASSIGN(networkTab_, button(kDockTabs, kBottom("Network")));
    RAWFRAME_TRY_ASSIGN(environmentTab_, button(kDockTabs, kBottom("Environment")));
    RAWFRAME_TRY_ASSIGN(profilerTab_, button(kDockTabs, kBottom("Profiler")));
    RAWFRAME_TRY_ASSIGN(held_.frameTab, button(kDockTabs, kBottom("Frame")));
    RAWFRAME_TRY_ASSIGN(held_.changesTab, button(kDockTabs, kBottom("Changes")));
    RAWFRAME_TRY_ASSIGN(held_.panelsTab, button(kDockTabs, kBottom("Extension panels")));
    RAWFRAME_TRY_ASSIGN(scenesColumn_, pane("Assets"));
    RAWFRAME_TRY_ASSIGN(historyColumn_, pane("History"));
    RAWFRAME_TRY(makeConsole());
    RAWFRAME_TRY(makeNetwork());
    RAWFRAME_TRY(makeEnvironment());
    RAWFRAME_TRY(makeProfiler());
    RAWFRAME_TRY(makeFrame());
    RAWFRAME_TRY(makeChanges());
    RAWFRAME_TRY(makeExtensionPanels());
    RAWFRAME_TRY(tree_->attach(bottomDock_, scenesColumn_));
    dockShown_ = Dock::Assets;
    showDock(Dock::Assets);
    RAWFRAME_TRY(splitter(kBody, Edge::Inspector));
    RAWFRAME_TRY_ASSIGN(componentsColumn_,
                        column(kBody, placed("Inspector", SurfaceKind::Panel, "Right"), preferences_.inspectorWidth));
    RAWFRAME_TRY(showMenus(kStart));
    RAWFRAME_TRY(showScenes());
    showModes();
    return {};
}

void ShellParticipant::showDock(Dock pane) {
    const auto kPaneOf = [this](Dock dock) {
        return dock == Dock::Assets        ? scenesColumn_
               : dock == Dock::History     ? historyColumn_
               : dock == Dock::Console     ? consoleColumn_
               : dock == Dock::Network     ? networkColumn_
               : dock == Dock::Environment ? held_.environmentColumn
               : dock == Dock::Profiler    ? held_.profilerColumn
               : dock == Dock::Frame       ? held_.frameColumn
               : dock == Dock::Changes     ? held_.changesColumn
                                           : held_.panelsColumn;
    };
    if (pane != dockShown_) {
        static_cast<void>(tree_->detach(kPaneOf(dockShown_)));
        static_cast<void>(tree_->attach(bottomDock_, kPaneOf(pane)));
        dockShown_ = pane;
    }
    static_cast<void>(tree_->setLook(assetsTab_, ui::Look{.fill = pane == Dock::Assets ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(historyTab_, ui::Look{.fill = pane == Dock::History ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(consoleTab_, ui::Look{.fill = pane == Dock::Console ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(networkTab_, ui::Look{.fill = pane == Dock::Network ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(environmentTab_, ui::Look{.fill = pane == Dock::Environment ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(profilerTab_, ui::Look{.fill = pane == Dock::Profiler ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(held_.frameTab, ui::Look{.fill = pane == Dock::Frame ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(held_.changesTab, ui::Look{.fill = pane == Dock::Changes ? kChosen : kRow, .radius = 4}));
    static_cast<void>(
        tree_->setLook(held_.panelsTab, ui::Look{.fill = pane == Dock::Panels ? kChosen : kRow, .radius = 4}));
    if (pane == Dock::Console) {
        showConsole();
    } else if (pane == Dock::Environment) {
        showEnvironment();
    } else if (pane == Dock::Changes) {
        readChanges();
    } else if (pane == Dock::Panels) {
        showExtensionPanels();
    }
}

void ShellParticipant::showTabs(std::optional<std::size_t> opened) {
    if (opened.has_value() && *opened < scenes_.size() &&
        std::ranges::find(openDocuments_, scenes_[*opened]) == openDocuments_.end()) {
        openDocuments_.push_back(scenes_[*opened]);
        // The oldest let go past eight, never the one chosen.
        if (openDocuments_.size() > 8) {
            openDocuments_.erase(openDocuments_.begin());
        }
    }
    // A document gone from the game, deleted or renamed outside, closes.
    std::erase_if(openDocuments_, [&](const std::string& path) {
        return std::ranges::find(scenes_, path) == scenes_.end();
    });
    clear(tabRows_);
    for (const std::string& kPath : openDocuments_) {
        auto tab = box(tabsRow_, ui::Layout{.shrink = 0, .padding = {10, 4, 10, 4}}, kPath == scene_ ? kChosen : kRow);
        if (!tab.has_value() || !pressWords(*tab, documentLabel(kPath), kText, 14).has_value()) {
            return;
        }
        tabRows_.push_back(*tab);
    }
}

result::Status ShellParticipant::showBroken(ui::Node body) {
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kBroken,
        box(body,
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
    // The browser's controls first, then the documents it shows (D799),
    // one left out holding no row, then the other sources.
    RAWFRAME_TRY(showBrowser());
    // A new scene is the session's own verb, not an operation the catalog
    // lists, so it is always offered, above the list so a long one does not
    // hide it. A file's absolute path typed there imports it instead (D505).
    RAWFRAME_TRY_ASSIGN(const auto kLine, fieldRow("new scene or import", "", scenesColumn_));
    newSceneRow_ = kLine.first;
    newScene_ = FieldRow{.value = kLine.second, .role = FieldRow::Role::NewScene, .field = "new scene"};
    for (std::size_t each = 0; each < scenes_.size(); ++each) {
        if (!browser_.shows(scenes_[each])) {
            sceneRows_.emplace_back();
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            row(scenesColumn_,
                                documentLabel(scenes_[each]),
                                kText,
                                each == sceneAt_ && !scene_.empty() ? kChosen : kRow,
                                true));
        sceneRows_.push_back(kRowNode);
    }
    RAWFRAME_TRY(showSources());
    showTabs();
    return {};
}

result::Result<ui::Node>
ShellParticipant::row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill, bool pressed) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                        box(column, ui::Layout{.height = ui::pixels(28), .shrink = 0, .padding = {8, 4, 8, 4}}, fill));
    RAWFRAME_TRY(pressed ? pressWords(kRowNode, text, color, 14) : words(kRowNode, text, color, 14));
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
        // What was chosen was the other scene's (D787).
        selected_.clear();
        anchor_.reset();
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
    showTabs(at);
    // The mode it is in, lit in the palette (D910).
    showModes();
    if (kChanged) {
        // A scene's view is the session's, unknown here until it says,
        // and its search starts empty. A style sheet has neither, nor a
        // preview (D637).
        view_.reset();
        find_.clear();
        static_cast<void>(tree_->scrollTo(entitiesColumn_, 0, 0));
        if (documentChosen()) {
            clear(viewRows_);
            viewFields_.clear();
            held_.canvasParts.clear();
            held_.canvasBoxes.clear();
            forgetUiPreview();
            showHistoryList();
        } else {
            showView();
            if (preview_.has_value()) {
                attachPreview();
            }
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
    parents_.clear();
    entityLayers_.clear();
    entity_.clear();
    components_ = 0;
    findField_.reset();
    held_.timelineScale = 0;
    // The played game's World in place of the document's (D808).
    if (live_.on) {
        showLive();
        return;
    }
    if (chosen(Listed::Sheet)) {
        showSheet();
        return;
    }
    if (chosen(Listed::Ui)) {
        showUi();
        return;
    }
    if (chosen(Listed::Material)) {
        showMaterial();
        return;
    }
    if (chosen(Listed::Graph)) {
        showGraph();
        return;
    }
    if (chosen(Listed::Data)) {
        showData();
        return;
    }
    if (chosen(Listed::Settings)) {
        showSettings();
        return;
    }
    if (chosen(Listed::Capture)) {
        showCapture();
        return;
    }
    if (chosen(Listed::Blockout)) {
        showBlockout();
        return;
    }
    if (chosen(Listed::Terrain)) {
        showTerrain();
        return;
    }
    if (chosen(Listed::Tilemap)) {
        showTilemap();
        return;
    }
    if (chosen(Listed::Scatter)) {
        showScatter();
        return;
    }
    if (chosen(Listed::Sequence)) {
        showSequence();
        return;
    }
    // Over the scene's entities, the search that chooses them (D473), then
    // the scene an instance placed in it is of: at the Hierarchy's top,
    // where a long scene does not scroll them away (D776).
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
    const std::optional<Value> kList = entitiesShown();
    const Value* listed = kList.has_value() ? kList->find("entities") : nullptr;
    if (listed == nullptr || listed->kind() != Value::Kind::Array) {
        return;
    }
    // Shown as a tree, each entity under the one it follows (D785).
    std::vector<const Value*> items;
    std::vector<std::string> ids;
    std::vector<std::string> parents;
    for (const Value& each : listed->items()) {
        const Value* id = each.find("id");
        const Value* parent = each.find("parent");
        if (id == nullptr || id->text() == nullptr) {
            continue;
        }
        items.push_back(&each);
        ids.push_back(*id->text());
        parents.push_back(parent != nullptr && parent->text() != nullptr ? *parent->text() : std::string{});
    }
    for (const TreeLine& kLine : treeOf(ids, parents)) {
        const Value& each = *items[kLine.at];
        const Value* id = each.find("id");
        const Value* name = each.find("name");
        const Value* brought = each.find("brought");
        const Value* removed = each.find("removed");
        const bool kRemoved = removed != nullptr && removed->truth().value_or(false);
        std::string label = name != nullptr && name->text() != nullptr ? *name->text() : std::string{};
        if (label.empty() && brought != nullptr) {
            const Value* instance = brought->find("instance");
            label = "instance " + (instance != nullptr ? document::writeCompact(*instance) : std::string{"?"}) + "  " +
                    id->text()->substr(0, 8);
        }
        if (kRemoved) {
            label += "  (removed)";
        }
        // Who else has it chosen, and who holds it, at a host joined (D884).
        label += presenceMarks(*id->text());
        auto added = row(entitiesColumn_, label, brought != nullptr ? kQuiet : kText, kRow, true);
        if (!added.has_value()) {
            break;
        }
        if (kLine.depth > 0) {
            static_cast<void>(
                tree_->setLayout(*added,
                                 ui::Layout{.height = ui::pixels(28),
                                            .shrink = 0,
                                            .padding = {8.0F + (16.0F * static_cast<float>(kLine.depth)), 4, 8, 4}}));
        }
        entityRows_.push_back(*added);
        parents_.push_back(parents[kLine.at]);
        const Value* layer = each.find("layer");
        entityLayers_.push_back(layer != nullptr ? static_cast<std::uint32_t>(layer->integer().value_or(0)) : 0);
        entities_.push_back(*id->text());
        names_.push_back(name != nullptr && name->text() != nullptr ? *name->text() : std::string{});
        brought_.push_back(brought != nullptr);
        removed_.push_back(kRemoved);
        const Value* place = each.find("place");
        places_.push_back(place != nullptr ? place->integer() : std::nullopt);
    }
    // The Layers count what the scene has now, and the environment says
    // what holds its light.
    if (leftShown_ == Left::Layers) {
        showLayers();
    }
    if (leftShown_ == Left::World) {
        showWorld();
    }
    if (dockShown_ == Dock::Environment) {
        showEnvironment();
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
    if (chosen(Listed::Sheet)) {
        showClass(at);
        return;
    }
    if (chosen(Listed::Ui)) {
        showNode(at);
        return;
    }
    if (chosen(Listed::Material)) {
        showInput(at);
        return;
    }
    if (chosen(Listed::Graph)) {
        showGraphNode(at);
        return;
    }
    if (chosen(Listed::Data)) {
        showDataEntry(at);
        return;
    }
    if (chosen(Listed::Settings)) {
        showSettingsGroup(at);
        return;
    }
    if (chosen(Listed::Capture)) {
        showCaptureEntry(at);
        return;
    }
    if (chosen(Listed::Blockout)) {
        showBlockoutSolid(at);
        return;
    }
    if (chosen(Listed::Terrain)) {
        showTerrainLayer(at);
        return;
    }
    if (chosen(Listed::Tilemap)) {
        showTileRow(at);
        return;
    }
    if (chosen(Listed::Scatter)) {
        reseedScatter();
        return;
    }
    if (chosen(Listed::Sequence)) {
        addSequenceTrack();
        return;
    }
    endEdit();
    fields_.clear();
    entityAt_ = at;
    if (entity_ != entities_[at]) {
        static_cast<void>(tree_->scrollTo(componentsColumn_, 0, 0));
    }
    entity_ = entities_[at];
    // One entity shown is the one chosen, so an edit reaches no other
    // (D787).
    selected_ = {entity_};
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    // Chosen in the session, so undo and redo keep it (D417).
    static_cast<void>(ask(selectRecord(next(), scene_, selected_)));
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
            // A labelled number by its label and a color as #RRGGBBAA
            // (D784); else a number naming a declared asset shows the
            // asset (D455).
            const Catalog::Field kType = kEach.type.value_or(Catalog::Field{.name = kEach.name, .kind = kEach.kind});
            if (kType.color || !kType.labels.empty()) {
                text = shownAs(kType, text);
            } else if (kEach.kind == "unsigned") {
                if (const Asset* held = assetHeld(assets_, text); held != nullptr) {
                    text = assetShown(*held);
                }
            }
            auto line = fieldRow(kEach.name, text, std::nullopt, kEach.text.has_value());
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            // Its unit and range beside it, and a color's swatch.
            if (const std::string kHint = hintText(kType); !kHint.empty()) {
                if (auto hint = box(line->first, ui::Layout{.shrink = 0}, 0); hint.has_value()) {
                    static_cast<void>(words(*hint, kHint, kQuiet, 13));
                }
            }
            if (const std::optional<std::uint32_t> kColor = kType.color ? colorOf(text) : std::nullopt;
                kColor.has_value()) {
                if (auto swatch = box(line->first,
                                      ui::Layout{.width = ui::pixels(18), .height = ui::pixels(18), .shrink = 0},
                                      *kColor);
                    swatch.has_value()) {
                    static_cast<void>(tree_->setName(*swatch, kEach.name + " swatch"));
                }
            }
            // An instance's own value, which it may drop for its source's.
            if (brought_[at] && kEach.text.has_value() && catalog_.offers("scene.revert_field")) {
                static_cast<void>(action(line->first,
                                         "Revert",
                                         ActionButton{.operation = "scene.revert_field",
                                                      .component = kComponent,
                                                      .field = kEach.name,
                                                      .done = kEach.name + " reverted"}));
            }
            fields_.push_back(FieldRow{.value = line->second,
                                       .component = kComponent,
                                       .field = kEach.name,
                                       .kind = kEach.kind,
                                       .type = kType});
        }
    }
    showSections(kRead);
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
    RAWFRAME_TRY(pressWords(asked.node, text, kQuiet, 13));
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
    // Read as a field named by its name, its text its value (D575).
    RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
    RAWFRAME_TRY(tree_->setName(kValue, name));
    return std::pair{kLine, kValue};
}

void ShellParticipant::showView() {
    clear(viewRows_);
    viewFields_.clear();
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
    forgetUiPreview();
    auto heading = row(centerColumn_, "View", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    viewRows_.push_back(*heading);
    for (const std::string_view kPart : {"eye", "target", "fieldOfView", "snap"}) {
        auto line = fieldRow(kPart == "fieldOfView" ? "field of view" : kPart, {}, centerColumn_);
        if (!line.has_value()) {
            return;
        }
        viewRows_.push_back(line->first);
        viewFields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::View, .field = std::string{kPart}});
    }
    showViewport();
    showViewText();
    showHistoryList();
}

void ShellParticipant::showHistoryList() {
    clear(historyRows_);
    entryRows_.clear();
    if (scene_.empty()) {
        return;
    }
    for (const HistoryEntry& entry : historyOf(ask(historyRecord(next(), scene_)))) {
        // An entry that takes another back says so, and one another author
        // made says whose it is (ADR-0085).
        std::string text =
            entry.kind == "undo" || entry.kind == "redo" ? entry.kind + ": " + entry.summary : entry.summary;
        if (!entry.author.empty()) {
            text += " by " + entry.author;
        }
        auto added = row(historyColumn_, text, entry.applied ? kText : kQuiet, entry.applied ? kRow : kField, true);
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
    RAWFRAME_TRY(pressWords(undoNode_, "Undo", undoable_ > 0 ? kText : kQuiet, 14));
    return pressWords(redoNode_, "Redo", redoable_ > 0 ? kText : kQuiet, 14);
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
        addConsoleLine(ConsoleLine{.source = ConsoleLine::Source::Studio, .text = status_});
    }
}

} // namespace rawframe::studio
