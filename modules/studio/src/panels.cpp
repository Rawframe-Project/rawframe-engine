#include "hierarchy.h"
#include "hints.h"
#include "shell.h"
#include "surfaces.h"

#include <algorithm>
#include <cctype>

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

result::Status ShellParticipant::strongWords(ui::Node node, std::string_view text, std::uint32_t color, float size) {
    return tree_->setText(node, text, ui::TextLook{.font = strongFont_, .size = size, .color = color, .wrap = false});
}

result::Status ShellParticipant::monoWords(ui::Node node, std::string_view text, std::uint32_t color, float size) {
    return tree_->setText(node, text, ui::TextLook{.font = monoFont_, .size = size, .color = color, .wrap = false});
}

result::Result<ui::Node> ShellParticipant::icon(ui::Node parent, Icon which, std::uint32_t color, float side) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kIcon,
                        box(parent, ui::Layout{.width = ui::pixels(side), .height = ui::pixels(side), .shrink = 0}, 0));
    RAWFRAME_TRY(tree_->setLook(kIcon, ui::Look{.image = look::iconImage(which), .imageTint = color}));
    RAWFRAME_TRY(tree_->setInteraction(kIcon, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    return kIcon;
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

void ShellParticipant::showTitle() {
    if (!tree_->contains(held_.titleGame)) {
        return;
    }
    if (!gameOpen()) {
        static_cast<void>(words(held_.titleGame, "Rawframe Studio", look::kInk, look::kBodySize));
        static_cast<void>(words(held_.titleDocument, "", look::kLabelInk, look::kBodySize));
        return;
    }
    static_cast<void>(words(held_.titleGame, description_.stem().string(), look::kInk, look::kBodySize));
    static_cast<void>(words(held_.titleDocument,
                            scene_.empty() ? std::string{} : "  \u00B7  " + documentLabel(scene_),
                            look::kLabelInk,
                            look::kBodySize));
}

result::Result<ui::Node> ShellParticipant::dockColumn(ui::Node parent, std::string_view name) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kColumn,
                        box(parent,
                            ui::Layout{.height = ui::pixels(0),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 1,
                                       .grow = 1,
                                       .padding = {6, 6, 8, 8},
                                       .scroll = ui::Scroll::Vertical},
                            0));
    RAWFRAME_TRY(tree_->setLook(kColumn, ui::Look{}));
    RAWFRAME_TRY(tree_->setRole(kColumn, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(kColumn, name));
    return kColumn;
}

result::Result<ui::Node> ShellParticipant::button(ui::Node parent, std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kButton,
                        box(parent, ui::Layout{.shrink = 0, .padding = {10, 10, 4, 4}, .border = {1, 1, 1, 1}}, 0));
    RAWFRAME_TRY(
        tree_->setLook(kButton, ui::Look{.fill = look::kRaisedFill, .borderColor = look::kRaisedEdge, .radius = 5}));
    RAWFRAME_TRY(pressWords(kButton, text, kText, look::kBodySize));
    return kButton;
}

result::Result<ui::Node> ShellParticipant::entityRow(ui::Node column,
                                                     std::string_view label,
                                                     std::size_t depth,
                                                     bool parent,
                                                     Icon which,
                                                     std::uint32_t iconInk,
                                                     std::uint32_t ink,
                                                     std::string_view quiet) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                        box(column,
                            ui::Layout{.height = ui::pixels(look::kRowHeight),
                                       .alignItems = ui::Align::Center,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {6.0F + (14.0F * static_cast<float>(depth)), 8, 0, 0}},
                            kRow));
    if (parent) {
        RAWFRAME_TRY(icon(kRowNode, Icon::ChevronDown, look::kFaintInk, 12));
    } else {
        RAWFRAME_TRY(box(kRowNode, ui::Layout{.width = ui::pixels(12), .shrink = 0}, 0));
    }
    RAWFRAME_TRY(icon(kRowNode, which, iconInk, 14));
    RAWFRAME_TRY_ASSIGN(const ui::Node kLabel, box(kRowNode, ui::Layout{.shrink = 1}, 0));
    RAWFRAME_TRY(words(kLabel, label, ink, look::kBodySize));
    RAWFRAME_TRY(tree_->setInteraction(kLabel, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    // What it is, quieter, after its name (D923).
    if (!quiet.empty()) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kQuietLabel, box(kRowNode, ui::Layout{.shrink = 1}, 0));
        RAWFRAME_TRY(monoWords(kQuietLabel, quiet, look::kFaintInk, look::kSmallSize));
        RAWFRAME_TRY(tree_->setInteraction(kQuietLabel, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    }
    RAWFRAME_TRY(tree_->setRole(kRowNode, ui::Role::Button));
    RAWFRAME_TRY(tree_->setName(kRowNode, label));
    return kRowNode;
}

result::Status ShellParticipant::pressWords(ui::Node node, std::string_view text, std::uint32_t color, float size) {
    RAWFRAME_TRY(words(node, text, color, size));
    // A node's text is read as its value, so a button is named by it too.
    RAWFRAME_TRY(tree_->setRole(node, ui::Role::Button));
    return tree_->setName(node, text);
}

result::Status ShellParticipant::build() {
    RAWFRAME_TRY_ASSIGN(
        root_,
        box(std::nullopt,
            ui::Layout{.width = ui::share(1), .height = ui::share(1), .direction = ui::Direction::Column},
            kBackground));
    RAWFRAME_TRY(tree_->setLook(root_, ui::Look{.fill = kBackground}));
    held_.buttonParts.clear();
    held_.commandButtons.clear();
    // The title row: the mark, the menus, what is open in the middle, the
    // palette's field at its end (D915).
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitleRow, titleRow(root_));
    RAWFRAME_TRY_ASSIGN(const ui::Node kStart, box(kTitleRow, ui::Layout{.alignItems = ui::Align::Center}, 0));
    RAWFRAME_TRY(box(kTitleRow, ui::Layout{.grow = 1}, 0));
    // The game and the document open in it, as "plaza · gate.scene".
    RAWFRAME_TRY_ASSIGN(held_.titleGame, box(kTitleRow, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(held_.titleDocument, box(kTitleRow, ui::Layout{.shrink = 0}, 0));
    showTitle();
    RAWFRAME_TRY(box(kTitleRow, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kSearchAt,
                        box(kTitleRow, ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0}, 0));
    // The others at a host joined, each in their color (D884).
    held_.avatars.clear();
    held_.avatarsNode = ui::Node{};
    if (held_.joined != nullptr) {
        RAWFRAME_TRY_ASSIGN(
            held_.avatarsNode,
            box(kTitleRow, ui::Layout{.alignItems = ui::Align::Center, .gap = 4, .margin = {8, 0, 0, 0}}, 0));
        showCollaborators();
    }
    held_.searchAt = kSearchAt;
    // The toolbar: tools and snapping, Cook and Play, history and layout;
    // the start screen has neither it nor the status bar (D918).
    if (gameOpen()) {
        RAWFRAME_TRY(toolbar(root_));
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kBody,
        box(root_, ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Row, .grow = 1}, kBackground));
    if (gameOpen()) {
        RAWFRAME_TRY(statusBar(root_));
    }
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
        return showMenus(kStart, held_.searchAt);
    }
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kMain,
        box(kBody, ui::Layout{.width = ui::pixels(0), .direction = ui::Direction::Column, .grow = 1}, kBackground));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kUpper,
        box(kMain,
            ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(0), .direction = ui::Direction::Row, .grow = 3},
            kBackground));
    // What a button stands for that the frame does not draw (D915): kept
    // apart from the window, so the menus, the keys, and the palette still
    // find what it does.
    RAWFRAME_TRY_ASSIGN(held_.shelf, box(std::nullopt, ui::Layout{}, 0));
    // The docks at the sizes the author left them (D779), each with a
    // splitter on its inner edge in place of the gap it had.
    // The left dock: its tabs, Hierarchy, Layers, and World (D788, D870),
    // then the one shown.
    RAWFRAME_TRY_ASSIGN(leftDock_, box(kUpper, leftLayout(preferences_.hierarchyWidth), look::kPanelFill));
    RAWFRAME_TRY(tree_->setLook(leftDock_, ui::Look{.fill = look::kPanelFill}));
    RAWFRAME_TRY_ASSIGN(const ui::Node kLeftTabs, tabStrip(leftDock_));
    RAWFRAME_TRY_ASSIGN(hierarchyTab_, tab(kLeftTabs, "Hierarchy", true));
    RAWFRAME_TRY_ASSIGN(layersTab_, tab(kLeftTabs, "Layers"));
    RAWFRAME_TRY_ASSIGN(worldTab_, tab(kLeftTabs, "World"));
    worldBackTab_ = ui::Node{};
    RAWFRAME_TRY(box(kLeftTabs, ui::Layout{.grow = 1}, 0));
    // The Hierarchy's own switch, the played World in place of the
    // document's (D808), and New, as the design's plus.
    RAWFRAME_TRY_ASSIGN(live_.button, textButton(kLeftTabs, std::nullopt, "Live", ButtonKind::Quiet));
    // The plus opens the New menu (D917); New itself, an empty entity, is
    // on the shelf.
    held_.newMenu = ui::Node{};
    if (catalog_.offers("scene.create_entity")) {
        RAWFRAME_TRY_ASSIGN(held_.newMenu, iconButton(kLeftTabs, Icon::Plus, "New entity"));
        RAWFRAME_TRY_ASSIGN(newNode_, button(held_.shelf, "New"));
    }
    RAWFRAME_TRY(box(kLeftTabs, ui::Layout{.width = ui::pixels(4), .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(entitiesColumn_, dockColumn(leftDock_, placed("Hierarchy", SurfaceKind::Panel, "Left")));
    RAWFRAME_TRY_ASSIGN(layersColumn_, dockColumn(leftDock_, placed("Layers", SurfaceKind::Panel, "Left")));
    RAWFRAME_TRY(tree_->detach(layersColumn_));
    RAWFRAME_TRY_ASSIGN(worldColumn_, dockColumn(leftDock_, placed("World", SurfaceKind::Panel, "Left")));
    RAWFRAME_TRY(tree_->detach(worldColumn_));
    leftShown_ = Left::Hierarchy;
    RAWFRAME_TRY(splitter(kUpper, Edge::Hierarchy));
    // The entities' other operations, on the shelf: the Entity and Edit
    // menus, the keys, and the palette reach them.
    if (catalog_.offers("scene.destroy_entity")) {
        RAWFRAME_TRY_ASSIGN(deleteNode_, button(held_.shelf, "Delete"));
    }
    // The chosen entity copied whole (D458), its operations the catalog's,
    // and moved to another scene, made there and deleted here (D498).
    if (catalog_.offers("scene.create_entity") && catalog_.offers("scene.add_component") &&
        catalog_.offers("scene.set_field")) {
        RAWFRAME_TRY_ASSIGN(duplicateNode_, button(held_.shelf, "Duplicate"));
        if (catalog_.offers("scene.destroy_entity")) {
            RAWFRAME_TRY_ASSIGN(moveNode_, button(held_.shelf, "Move to"));
        }
    }
    // An instance's entity: one it removed taken back, or the whole
    // instance removed (D450).
    if (catalog_.offers("scene.restore_entity")) {
        RAWFRAME_TRY_ASSIGN(restoreNode_, button(held_.shelf, "Restore"));
    }
    if (catalog_.offers("scene.remove_instance")) {
        RAWFRAME_TRY_ASSIGN(uninstanceNode_, button(held_.shelf, "Remove instance"));
    }
    // The chosen entity's place among the scene's own (D451).
    if (catalog_.offers("scene.move_entity")) {
        RAWFRAME_TRY_ASSIGN(upNode_, button(held_.shelf, "Up"));
        RAWFRAME_TRY_ASSIGN(downNode_, button(held_.shelf, "Down"));
    }
    // The center: the open documents' tabs, then the active one, a
    // scene's the viewport (state.md item 28 step 5).
    RAWFRAME_TRY_ASSIGN(
        centerColumn_,
        box(kUpper,
            ui::Layout{
                .width = ui::pixels(0), .minHeight = ui::pixels(0), .direction = ui::Direction::Column, .grow = 2},
            look::kPanelFill));
    RAWFRAME_TRY(tree_->setLook(centerColumn_, ui::Look{.fill = look::kPanelFill}));
    RAWFRAME_TRY(tree_->setName(centerColumn_, "Documents"));
    RAWFRAME_TRY_ASSIGN(tabsRow_, tabStrip(centerColumn_));
    RAWFRAME_TRY(tree_->setLook(tabsRow_, ui::Look{.fill = look::kBarFill, .borderColor = look::kLine}));
    // The mode palette on the viewport's left edge (SPEC-0051, D910), over
    // it, its buttons the new tree's.
    held_.modeButtons.clear();
    RAWFRAME_TRY_ASSIGN(
        held_.modePalette,
        box(centerColumn_,
            ui::Layout{.direction = ui::Direction::Column,
                       .gap = 2,
                       .padding = {4, 4, 4, 4},
                       .border = {1, 1, 1, 1},
                       .placement = {.absolute = true, .x = ui::pixels(8), .y = ui::pixels(look::kTabHeight + 44)}},
            0));
    RAWFRAME_TRY(
        tree_->setLook(held_.modePalette, ui::Look{.fill = 0x121317E0U, .borderColor = 0xFFFFFF14U, .radius = 6}));
    RAWFRAME_TRY(tree_->setName(held_.modePalette, "Mode palette"));
    // The bottom dock under the Hierarchy and the center, its tabs its
    // heading.
    RAWFRAME_TRY(splitter(kMain, Edge::Dock));
    RAWFRAME_TRY_ASSIGN(bottomDock_, box(kMain, dockLayout(preferences_.dockHeight), look::kPanelFill));
    RAWFRAME_TRY(tree_->setLook(bottomDock_, ui::Look{.fill = look::kPanelFill}));
    // Each panel by the name the surface table places it under (D873).
    constexpr auto kBottom = [](std::string_view name) consteval {
        return placed(name, SurfaceKind::Panel, "Bottom");
    };
    RAWFRAME_TRY_ASSIGN(const ui::Node kDockTabs, tabStrip(bottomDock_));
    RAWFRAME_TRY_ASSIGN(assetsTab_, tab(kDockTabs, kBottom("Assets"), true));
    RAWFRAME_TRY_ASSIGN(consoleTab_, tab(kDockTabs, kBottom("Console")));
    RAWFRAME_TRY_ASSIGN(historyTab_, tab(kDockTabs, kBottom("History")));
    RAWFRAME_TRY_ASSIGN(networkTab_, tab(kDockTabs, kBottom("Network")));
    RAWFRAME_TRY_ASSIGN(profilerTab_, tab(kDockTabs, kBottom("Profiler")));
    RAWFRAME_TRY_ASSIGN(held_.frameTab, tab(kDockTabs, kBottom("Frame")));
    RAWFRAME_TRY_ASSIGN(held_.changesTab, tab(kDockTabs, kBottom("Changes")));
    RAWFRAME_TRY_ASSIGN(held_.panelsTab, tab(kDockTabs, kBottom("Extension panels")));
    RAWFRAME_TRY(makeAssets());
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
    // The right dock, full height: its tabs, then the Inspector.
    RAWFRAME_TRY(splitter(kBody, Edge::Inspector));
    RAWFRAME_TRY_ASSIGN(held_.rightDock,
                        box(kBody,
                            ui::Layout{.width = ui::pixels(preferences_.inspectorWidth),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .shrink = 0,
                                       .border = {1, 0, 0, 0}},
                            look::kPanelFill));
    RAWFRAME_TRY(tree_->setLook(held_.rightDock, ui::Look{.fill = look::kPanelFill, .borderColor = look::kLine}));
    RAWFRAME_TRY_ASSIGN(const ui::Node kRightTabs, tabStrip(held_.rightDock));
    RAWFRAME_TRY_ASSIGN(held_.inspectorTab, tab(kRightTabs, "Inspector", true));
    RAWFRAME_TRY_ASSIGN(held_.sceneSettingsTab, tab(kRightTabs, "Scene settings"));
    // The Environment panel is the Scene settings tab, as the design's
    // (D921).
    environmentTab_ = held_.sceneSettingsTab;
    held_.sceneSettingsShown = false;
    RAWFRAME_TRY_ASSIGN(componentsColumn_,
                        dockColumn(held_.rightDock, placed("Inspector", SurfaceKind::Panel, "Right")));
    RAWFRAME_TRY(showMenus(kStart, held_.searchAt));
    RAWFRAME_TRY(showScenes());
    showModes();
    return {};
}

void ShellParticipant::showDock(Dock pane) {
    // The Environment is the right dock's Scene settings (D921).
    if (pane == Dock::Environment) {
        showRight(true);
        return;
    }
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
    setTab(assetsTab_, pane == Dock::Assets);
    setTab(historyTab_, pane == Dock::History);
    setTab(consoleTab_, pane == Dock::Console);
    setTab(networkTab_, pane == Dock::Network);
    setTab(environmentTab_, pane == Dock::Environment);
    setTab(profilerTab_, pane == Dock::Profiler);
    setTab(held_.frameTab, pane == Dock::Frame);
    setTab(held_.changesTab, pane == Dock::Changes);
    setTab(held_.panelsTab, pane == Dock::Panels);
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
        // As the design's document tabs (D915): the open one lit, its top
        // the accent, each with its kind's icon.
        const bool kOn = kPath == scene_;
        auto tab = box(tabsRow_,
                       ui::Layout{.alignItems = ui::Align::Center,
                                  .gap = 8,
                                  .shrink = 0,
                                  .padding = {12, 12, kOn ? 6.0F : 8.0F, 7},
                                  .border = {0, 0, kOn ? 2.0F : 0.0F, 0}},
                       0);
        if (!tab.has_value()) {
            return;
        }
        static_cast<void>(
            tree_->setLook(*tab, ui::Look{.fill = kOn ? look::kPanelFill : 0, .borderColor = look::kAccent}));
        const std::size_t kAt = static_cast<std::size_t>(std::ranges::find(scenes_, kPath) - scenes_.begin());
        const Listed kKind = kAt < listed_.size() ? listed_[kAt] : Listed::Scene;
        const Icon kIcon = kKind == Listed::Scene                                ? Icon::Cube
                           : kKind == Listed::Material || kKind == Listed::Graph ? Icon::Material
                           : kKind == Listed::Ui || kKind == Listed::Sheet       ? Icon::Ui
                                                                                 : Icon::File;
        static_cast<void>(icon(*tab, kIcon, kOn ? look::kInk : look::kLabelInk, 14));
        auto label = box(*tab, ui::Layout{.shrink = 0}, 0);
        if (!label.has_value() ||
            !words(*label, documentLabel(kPath), kOn ? look::kInk : look::kLabelInk, look::kBodySize).has_value()) {
            return;
        }
        static_cast<void>(tree_->setInteraction(*label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        static_cast<void>(tree_->setRole(*tab, ui::Role::Button));
        static_cast<void>(tree_->setName(*tab, documentLabel(kPath)));
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
    newScene_.reset();
    // The browser's heading and folders first, then the documents it
    // shows (D799), one left out holding no card, then the other sources.
    RAWFRAME_TRY(showBrowser());
    RAWFRAME_TRY(showDocuments());
    RAWFRAME_TRY(showSources());
    showTabs();
    return {};
}

result::Result<ui::Node>
ShellParticipant::row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill, bool pressed) {
    RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode, box(column, ui::Layout{.shrink = 0, .padding = {8, 8, 4, 3}}, fill));
    // A row as the design's: clear until chosen, its words a row's (D915).
    const std::uint32_t kInk = color == kText ? look::kRowInk : color;
    RAWFRAME_TRY(pressed ? pressWords(kRowNode, text, kInk, look::kBodySize)
                         : words(kRowNode, text, kInk, look::kBodySize));
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
        // What was chosen was the other scene's (D787), and a source chosen
        // in Assets gives the Inspector up to it (D916).
        selected_.clear();
        browser_.chosen.clear();
        for (const auto& [kCard, kPath] : browser_.sourceRows) {
            lightAsset(kCard, false);
        }
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
    showTitle();
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
        if (sceneRows_[each] != ui::Node{}) {
            lightAsset(sceneRows_[each], each == at);
        }
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
    // As the design's filter (D916): a well with the magnifier, then the
    // instance's, each its hint until typed into.
    if (catalog_.offers("scene.find_entities")) {
        auto line = searchWell(entitiesColumn_, "find", "Filter entities", find_, 0, Icon::Search);
        if (line.has_value()) {
            static_cast<void>(tree_->setLayout(line->first,
                                               ui::Layout{.height = ui::pixels(26),
                                                          .alignItems = ui::Align::Center,
                                                          .gap = 6,
                                                          .shrink = 0,
                                                          .padding = {8, 4, 0, 0},
                                                          .margin = {2, 2, 0, 4},
                                                          .border = {1, 1, 1, 1}}));
            sceneFieldRows_.push_back(line->first);
            findField_ = FieldRow{
                .value = line->second, .role = FieldRow::Role::Find, .field = "find", .hint = "Filter entities"};
        }
    }
    if (catalog_.offers("scene.add_instance")) {
        auto line = searchWell(entitiesColumn_, "instance of", "Place an instance of a scene", "", 0, Icon::Prefab);
        if (line.has_value()) {
            static_cast<void>(tree_->setLayout(line->first,
                                               ui::Layout{.height = ui::pixels(26),
                                                          .alignItems = ui::Align::Center,
                                                          .gap = 6,
                                                          .shrink = 0,
                                                          .padding = {8, 4, 0, 0},
                                                          .margin = {2, 2, 0, 6},
                                                          .border = {1, 1, 1, 1}}));
            sceneFieldRows_.push_back(line->first);
            sceneFields_.push_back(FieldRow{.value = line->second,
                                            .role = FieldRow::Role::Instance,
                                            .field = "instance of",
                                            .hint = "Place an instance of a scene"});
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
    const std::vector<TreeLine> lines = treeOf(ids, parents);
    for (const TreeLine& kLine : lines) {
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
        // A tree row (D915): an instance's entities are its scene's, in the
        // prefab's color.
        const std::size_t kPlace = static_cast<std::size_t>(&kLine - lines.data());
        const bool kParent = kPlace + 1 < lines.size() && lines[kPlace + 1].depth > kLine.depth;
        // Its icon by what its name says it is, as an editor's outliner
        // tells a light from a mesh (D923).
        std::string lower = label;
        std::ranges::transform(lower, lower.begin(), [](char each) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(each)));
        });
        const auto kNamed = [&lower](std::string_view part) {
            return lower.find(part) != std::string::npos;
        };
        const auto [kIcon, kInk] = brought != nullptr ? std::pair{Icon::Prefab, look::kInstanceInk}
                                   : kNamed("sun")    ? std::pair{Icon::Sun, look::kWarn}
                                   : kNamed("sky")    ? std::pair{Icon::Sky, look::kAccent}
                                   : kNamed("lamp") || kNamed("light") || kNamed("bulb")
                                       ? std::pair{Icon::Bulb, look::kWarn}
                                   : kNamed("camera")                    ? std::pair{Icon::Camera, look::kLabelInk}
                                   : kNamed("player") || kNamed("spawn") ? std::pair{Icon::Person, look::kEntityInk}
                                                                         : std::pair{Icon::Cube, look::kEntityInk};
        auto added = entityRow(entitiesColumn_,
                               label,
                               kLine.depth,
                               kParent,
                               kIcon,
                               kInk,
                               brought != nullptr ? 0xC9B2FFFFU : look::kRowInk);
        if (!added.has_value()) {
            break;
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
    if (held_.sceneSettingsShown) {
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

void ShellParticipant::showRight(bool sceneSettings) {
    // The right dock's two tabs: the Inspector, or the scene's settings,
    // the Environment panel (D921).
    if (sceneSettings != held_.sceneSettingsShown && tree_->contains(held_.rightDock)) {
        const ui::Node kOut = sceneSettings ? componentsColumn_ : held_.environmentColumn;
        const ui::Node kIn = sceneSettings ? held_.environmentColumn : componentsColumn_;
        static_cast<void>(tree_->detach(kOut));
        static_cast<void>(tree_->attach(held_.rightDock, kIn));
        held_.sceneSettingsShown = sceneSettings;
    }
    setTab(held_.inspectorTab, !sceneSettings);
    setTab(held_.sceneSettingsTab, sceneSettings);
    if (sceneSettings) {
        showEnvironment();
    }
}

void ShellParticipant::showEntity(std::size_t at) {
    // What is chosen shows in the Inspector.
    if (held_.sceneSettingsShown) {
        showRight(false);
    }
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
    // The Inspector's head (D915): the entity's icon and its name, the
    // scene's own entity named here, an instance's its source's.
    if (auto head = inspectorHead(names_[at], brought_[at], catalog_.offers("scene.rename_entity") && !brought_[at]);
        head.has_value() && head->has_value()) {
        fields_.push_back(FieldRow{.value = **head, .role = FieldRow::Role::Name, .field = "name"});
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
        const std::vector<FieldShown> kFieldsShown = fieldsShown(catalog_.component(kComponent), each.find("fields"));
        // Runs of axes shown as one row of colored X, Y, and Z (D915).
        std::size_t skip = 0;
        for (std::size_t place = 0; place < kFieldsShown.size(); ++place) {
            if (skip > 0) {
                --skip;
                continue;
            }
            if (const std::size_t kAxes = axesAt(kFieldsShown, place); kAxes > 0) {
                if (!vectorRow(kComponent,
                               name != nullptr && name->text() != nullptr ? *name->text() : std::string{},
                               std::span{kFieldsShown}.subspan(place, kAxes))
                         .has_value()) {
                    return;
                }
                skip = kAxes - 1;
                continue;
            }
            const FieldShown& kEach = kFieldsShown[place];
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
            // Unset, its default shown quiet (D915): nought, false, or its
            // first case.
            if (!kEach.text.has_value()) {
                text = !kType.cases.empty()    ? kType.cases.front()
                       : kEach.kind == "truth" ? std::string{"false"}
                       : kEach.kind == "real" || kEach.kind == "signed" || kEach.kind == "unsigned" ? std::string{"0"}
                                                                                                    : std::string{};
                // A labelled number's default by its label (D916).
                if (!kType.labels.empty()) {
                    text = shownAs(kType, text);
                }
            }
            auto line = fieldRow(kEach.name, text, std::nullopt, kEach.text.has_value(), prettyField(kEach.name));
            if (!line.has_value()) {
                return;
            }
            componentRows_.push_back(line->first);
            // A truth checked in its box, a field of a few values chosen
            // from its list (D916).
            if (kEach.kind == "truth") {
                asCheckbox(line->second, text == "true");
            } else if (!kType.cases.empty() || !kType.labels.empty()) {
                asChooser(line->first);
            }
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
    // As the design's Add component (D916): pressed, the catalog's
    // components listed under it, narrowed as a name is typed.
    if (catalog_.offers("scene.add_component")) {
        auto line = searchWell(componentsColumn_, "add", "Add component", "", 0, Icon::Plus);
        if (line.has_value()) {
            static_cast<void>(tree_->setLayout(line->first,
                                               ui::Layout{.height = ui::pixels(30),
                                                          .alignItems = ui::Align::Center,
                                                          .gap = 6,
                                                          .shrink = 0,
                                                          .padding = {10, 6, 0, 0},
                                                          .margin = {6, 6, 10, 8},
                                                          .border = {1, 1, 1, 1}}));
            static_cast<void>(tree_->setLook(
                line->first, ui::Look{.fill = look::kRaisedFill, .borderColor = look::kRaisedEdge, .radius = 5}));
            componentRows_.push_back(line->first);
            fields_.push_back(
                FieldRow{.value = line->second, .role = FieldRow::Role::Add, .field = "add", .hint = "Add component"});
        }
    }
}

result::Status
ShellParticipant::componentHeading(std::string_view name, const std::string& component, bool brought, bool stale) {
    // As the design's section (D915): a line above, the chevron, the
    // component's own name strong, its full name quiet in the fixed face,
    // and its buttons at its end.
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeading,
                        box(componentsColumn_,
                            ui::Layout{.alignItems = ui::Align::Center,
                                       .gap = 8,
                                       .shrink = 0,
                                       .padding = {6, 4, 6, 6},
                                       .margin = {0, 0, 6, 0},
                                       .border = {0, 0, 1, 0}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kHeading, ui::Look{.borderColor = 0x24262CFFU}));
    componentRows_.push_back(kHeading);
    RAWFRAME_TRY(icon(kHeading, Icon::ChevronDown, look::kFaintInk, 12));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeading, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(strongWords(kTitle, prettyComponent(name), look::kInk));
    RAWFRAME_TRY_ASSIGN(const ui::Node kOwner, box(kHeading, ui::Layout{.shrink = 1}, 0));
    RAWFRAME_TRY(monoWords(kOwner, name, look::kFaintInk, look::kSmallSize));
    RAWFRAME_TRY(box(kHeading, ui::Layout{.grow = 1}, 0));
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
    RAWFRAME_TRY_ASSIGN(asked.node, box(row, ui::Layout{.padding = {6, 6, 2, 2}, .margin = {2, 2, 0, 0}}, 0));
    RAWFRAME_TRY(pressWords(asked.node, text, look::kFaintInk, look::kLabelSize));
    actions_.push_back(std::move(asked));
    return {};
}

result::Result<std::pair<ui::Node, ui::Node>> ShellParticipant::fieldRow(
    std::string_view name, std::string_view value, std::optional<ui::Node> column, bool set, std::string_view shown) {
    // As the design's property row (D915): a label of 96 pixels, then the
    // field's well.
    RAWFRAME_TRY_ASSIGN(const ui::Node kLine,
                        box(column.value_or(componentsColumn_),
                            ui::Layout{.direction = ui::Direction::Row,
                                       .alignItems = ui::Align::Center,
                                       .gap = 10,
                                       .shrink = 0,
                                       .padding = {6, 6, 3, 3}},
                            0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kLine, ui::Layout{.width = ui::pixels(96), .shrink = 0}, 0));
    // A field the scene sets reads brighter than one at its default.
    RAWFRAME_TRY(words(kName, shown.empty() ? name : shown, set ? look::kSoftInk : look::kLabelInk, look::kLabelSize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kValue,
                                  ui::Layout{.width = ui::pixels(0),
                                             .minHeight = ui::pixels(25),
                                             .grow = 1,
                                             .padding = {8, 8, 3, 3},
                                             .border = {1, 1, 1, 1}}));
    RAWFRAME_TRY(tree_->setLook(kValue, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 4}));
    RAWFRAME_TRY(tree_->attach(kLine, kValue));
    RAWFRAME_TRY(words(kValue, value, set ? kText : look::kFaintInk, look::kBodySize));
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
    // The view's numbers kept on the shelf (D915): the viewport's camera
    // is flown, framed, and bookmarked, and these follow it.
    auto heading = row(held_.shelf, "View", kQuiet, kPanel);
    if (!heading.has_value()) {
        return;
    }
    viewRows_.push_back(*heading);
    for (const std::string_view kPart : {"eye", "target", "fieldOfView", "snap"}) {
        auto line = fieldRow(kPart == "fieldOfView" ? "field of view" : kPart, {}, held_.shelf);
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
    showAxes();
}

result::Status ShellParticipant::showHistory() {
    setIconButton(undoNode_, false, undoable_ > 0);
    setIconButton(redoNode_, false, redoable_ > 0);
    return {};
}

void ShellParticipant::say(std::string text) {
    // Logged as it changes: a game that ended is said again every half
    // second, and was logged as often (D495).
    const bool kChanged = text != status_;
    status_ = std::move(text);
    static_cast<void>(words(statusNode_, status_, look::kLabelInk, look::kLabelSize));
    if (kChanged && !status_.empty()) {
        emitter_.log(diagnostics::Severity::Info,
                     kSaid,
                     "Studio's status line says",
                     {diagnostics::field("status", std::string_view{status_})});
        addConsoleLine(ConsoleLine{.source = ConsoleLine::Source::Studio, .text = status_});
    }
}

} // namespace rawframe::studio
