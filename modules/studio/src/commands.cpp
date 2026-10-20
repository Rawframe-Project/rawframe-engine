#include "shell.h"
#include "surfaces.h"

#include <algorithm>
#include <cctype>

namespace rawframe::studio {

namespace {

/// A command's line in a menu or the palette: its name and its first chord,
/// the Delete key as menus show it.
std::string lineOf(const Command& command) {
    if (command.keys.empty()) {
        return command.name;
    }
    return command.name + "  " + (command.keys.front() == "Delete" ? std::string{"Del"} : command.keys.front());
}

std::string lowered(std::string_view text) {
    std::string low{text};
    std::ranges::transform(low, low.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return low;
}

/// The menus in the menu bar's order, none named as a command is, so a
/// screen reader's "Play" is the button (D777).
constexpr std::string_view kMenus[] = {"File", "Edit", "Entity", "Game", "Extensions", "View", "Window", "Help"};

/// The Help menu's commands (D930): the keys each command has, and what
/// Studio is, how its viewport is flown, and what it found beside it, each
/// a page of the preferences, as nothing there is configuration.
constexpr std::string_view kShortcuts = "Keyboard shortcuts";
constexpr std::string_view kAbout = "About Rawframe Studio";

/// The palette's own command, which has no button: it gives the palette's
/// field the keyboard.
constexpr std::string_view kPalette = placed("Command palette", SurfaceKind::Command, "Menu bar");

/// Studio's preferences, a dialog of its own with no button.
constexpr std::string_view kPreferences = placed("Preferences", SurfaceKind::Dialog, "Window menu");
/// The layouts' commands (D911): each its name and " layout", and the
/// docks saved as the author's next.
constexpr std::string_view kLayoutSuffix = " layout";
constexpr std::string_view kSaveLayout = "Save layout";

/// How many viewport modes the extensions may add: SPEC-0051's eight, less
/// Studio's own in the surface table (D877).
constexpr std::size_t kModesLeft =
    8 - static_cast<std::size_t>(std::ranges::count(kSurfaces, SurfaceKind::ViewportMode, &Surface::kind));

/// The game exported, a dialog of its own with no button (D872).
constexpr std::string_view kExport = placed("Export", SurfaceKind::Dialog, "File menu");

/// The view turned on what is chosen (D792), with no button: the key,
/// the menu, or the palette.
constexpr std::string_view kFrame = "Frame chosen";

/// The chosen entity claimed at a host joined, and let go (D884).
constexpr std::string_view kClaim = "Claim chosen";
constexpr std::string_view kRelease = "Release chosen";

/// The view seen in 2D, straight down at a 2D game's plane, or out of it
/// again (D793); no buttons either.
constexpr std::string_view kFlat = "2D view";
constexpr std::string_view kTilted = "3D view";

/// The device a play's first client is tried as (D809): this machine,
/// or a window of a phone's or a small tablet's size, the mouse a finger.
constexpr std::array<std::pair<std::string_view, std::array<std::uint32_t, 2>>, 3> kDevices = {
    {{"As this machine", {0, 0}}, {"As a phone", {360, 640}}, {"As a tablet", {960, 600}}}};

/// How many players a play has, each a client (D804).
constexpr std::array<std::pair<std::string_view, std::uint32_t>, 4> kPlayers = {
    {{"One player", 1}, {"Two players", 2}, {"Three players", 3}, {"Four players", 4}}};

/// What the preview draws over its scene, each toggled (D798), by the
/// renderer's name.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kOverlays = {
    {{"Show grid", "grid"}, {"Show colliders", "colliders"}, {"Show lights", "lights"}, {"Show game UI", "ui"}}};

/// The preview split one, two, or three ways (D797).
constexpr std::array<std::pair<std::string_view, std::uint8_t>, 3> kSplits = {
    {{"One view", 1}, {"Two views", 2}, {"Three views", 3}}};

/// A bookmark's commands, each followed by its slot (D796).
constexpr std::string_view kGoTo = "Go to bookmark ";
constexpr std::string_view kKeep = "Set bookmark ";

/// How the preview draws (D794), each by the renderer's name for it.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kModes = {
    {{"Lit", "lit"}, {"Unlit", "unlit"}, {"Normals", "normals"}}};

/// A menu's card: raised over the window, edged, with a shadow (D923).
constexpr ui::Look kMenuLook{.fill = 0x1E2026FFU,
                             .borderColor = look::kRaisedEdge,
                             .radius = 7,
                             .outerShadow = {.color = 0x00000080U, .y = 6, .blur = 18}};

/// The File menu's commands in its order, each with its key (D923).
constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kFileCommands = {{
    {"New game", ""},
    {"Open game", "Ctrl+O"},
    {"Save", "Ctrl+S"},
    {"Import files", "Ctrl+I"},
    {kExport, ""},
    {"Close game", ""},
    {"Quit", "Ctrl+Q"},
}};

/// The commands a menu draws a line above, each beginning a group of its
/// own as an editor's menus do.
/// The Extensions menu's own commands, after the extensions' (D938).
constexpr std::string_view kManageExtensions = "Manage extensions";
constexpr std::string_view kReloadExtensions = "Reload extensions";

constexpr std::array<std::string_view, 11> kGroupsBegun = {"Save",
                                                           "Import files",
                                                           "Close game",
                                                           "Quit",
                                                           "Duplicate",
                                                           "Move to",
                                                           "Assets",
                                                           kPalette,
                                                           kSaveLayout,
                                                           "Select tool",
                                                           kManageExtensions};

/// What the palette's field reads while it does not hold the keyboard.
constexpr std::string_view kPaletteHint = "Search commands, assets, entities    Ctrl K";

} // namespace

result::Status ShellParticipant::showMenus(ui::Node bar, std::optional<ui::Node> searchAt) {
    commands_.clear();
    menus_.clear();
    // A command's keys are its own unless the preferences give it others
    // (D782).
    const auto kKeys = [&](std::string_view name, std::vector<std::string> keys) {
        defaultKeys_[std::string{name}] = keys;
        const auto kGiven = preferences_.keys.find(name);
        return kGiven != preferences_.keys.end() ? kGiven->second : keys;
    };
    // Only what a button does now: an operation the catalog does not offer
    // has no button, and so no command (D777).
    const auto kOffer =
        [&](std::string_view name, std::string_view menu, std::vector<std::string> keys, ui::Node button) {
            if (tree_->contains(button)) {
                commands_.push_back(Command{.name = std::string{name},
                                            .menu = std::string{menu},
                                            .keys = kKeys(name, std::move(keys)),
                                            .button = button});
            }
        };
    kOffer("Cook", "Game", {"Ctrl+B"}, cookNode_);
    kOffer("Undo", "Edit", {"Ctrl+Z"}, undoNode_);
    kOffer("Redo", "Edit", {"Ctrl+Y", "Ctrl+Shift+Z"}, redoNode_);
    kOffer("Duplicate", "Edit", {"Ctrl+D"}, duplicateNode_);
    kOffer("Delete", "Edit", {"Delete"}, deleteNode_);
    kOffer("New", "Entity", {"Ctrl+Shift+N"}, newNode_);
    // The parts the game can be given (D917), in the Entity menu and under
    // the Hierarchy's plus.
    if (tree_->contains(newNode_)) {
        for (const std::string& kName : partCommands()) {
            commands_.push_back(Command{.name = kName, .menu = "Entity", .keys = kKeys(kName, {})});
        }
    }
    kOffer("Move to", "Entity", {}, moveNode_);
    kOffer("Restore", "Entity", {}, restoreNode_);
    kOffer("Remove instance", "Entity", {}, uninstanceNode_);
    kOffer("Up", "Entity", {}, upNode_);
    kOffer("Down", "Entity", {}, downNode_);
    kOffer("Play", "Game", {"F5"}, playNode_);
    // The viewport's tools by their keys, as the toolbar names them (D930);
    // while a press flies the view the keys are the flight's.
    for (const auto& [kName, kKey, kAt] :
         {std::tuple<std::string_view, std::string_view, std::size_t>{"Select tool", "Q", 0},
          std::tuple<std::string_view, std::string_view, std::size_t>{"Move tool", "W", 1},
          std::tuple<std::string_view, std::string_view, std::size_t>{"Rotate tool", "E", 2},
          std::tuple<std::string_view, std::string_view, std::size_t>{"Scale tool", "R", 3}}) {
        kOffer(kName, "Edit", {std::string{kKey}}, held_.tools[kAt]);
    }
    kOffer("Assets", "Window", {}, assetsTab_);
    kOffer("History", "Window", {}, historyTab_);
    kOffer("Console", "Window", {}, consoleTab_);
    kOffer("Network", "Window", {}, networkTab_);
    kOffer("Environment", "Window", {}, environmentTab_);
    kOffer("Profiler", "Window", {}, profilerTab_);
    kOffer("Frame", "Window", {}, held_.frameTab);
    kOffer("Timeline", "Window", {}, held_.timelineTab);
    kOffer("Changes", "Window", {}, held_.changesTab);
    kOffer("Extension panels", "Window", {}, held_.panelsTab);
    // The menus whose commands come below, with no button, always shown.
    for (const std::string_view kMenu : kMenus) {
        if (std::ranges::none_of(commands_,
                                 [&](const Command& command) {
                                     return command.menu == kMenu;
                                 }) &&
            kMenu != "File" && kMenu != "Window" && kMenu != "View" && kMenu != "Help" &&
            (kMenu != "Extensions" || !gameOpen())) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kButton, box(bar, ui::Layout{.shrink = 0, .padding = {9, 9, 4, 4}}, 0));
        RAWFRAME_TRY(pressWords(kButton, kMenu, look::kSoftInk, look::kBodySize));
        menus_.emplace_back(kButton, std::string{kMenu});
    }
    if (held_.layoutChooser != ui::Node{}) {
        menus_.emplace_back(held_.layoutChooser, std::string{kLayoutChooser});
    }
    if (tree_->contains(held_.newMenu)) {
        menus_.emplace_back(held_.newMenu, "New entity");
    }
    for (const auto& [kPill, kMenu] : held_.viewPills) {
        if (tree_->contains(kPill)) {
            menus_.emplace_back(kPill, kMenu);
        }
    }
    commands_.push_back(Command{.name = std::string{kPalette}, .menu = "Window", .keys = kKeys(kPalette, {"Ctrl+K"})});
    commands_.push_back(Command{.name = std::string{kPreferences}, .menu = "Window", .keys = kKeys(kPreferences, {})});
    commands_.push_back(Command{.name = std::string{kShortcuts}, .menu = "Help", .keys = kKeys(kShortcuts, {})});
    commands_.push_back(Command{.name = std::string{kAbout}, .menu = "Help", .keys = kKeys(kAbout, {"F1"})});
    // The layouts, where the surface table places "Docks and saved
    // layouts".
    for (const Layout& layout : layouts()) {
        const std::string kName = layout.name + std::string{kLayoutSuffix};
        commands_.push_back(Command{.name = kName, .menu = "Window", .keys = kKeys(kName, {})});
    }
    commands_.push_back(Command{.name = std::string{kSaveLayout}, .menu = "Window", .keys = kKeys(kSaveLayout, {})});
    // The File menu as an editor's (D923): a game made, opened, and
    // closed, what is saved, files brought in and the game sent out, and
    // Studio quit.
    for (const auto& [kName, kKey] : kFileCommands) {
        if (kName == kExport) {
            commands_.push_back(Command{.name = std::string{kExport}, .menu = "File", .keys = kKeys(kExport, {})});
            continue;
        }
        commands_.push_back(Command{
            .name = std::string{kName},
            .menu = "File",
            .keys =
                kKeys(kName, kKey.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string{kKey}})});
    }
    commands_.push_back(Command{.name = std::string{kFrame}, .menu = "View", .keys = kKeys(kFrame, {"F"})});
    // At a host joined, the chosen entity claimed and let go (D884).
    if (held_.joined != nullptr) {
        commands_.push_back(Command{.name = std::string{kClaim}, .menu = "Edit", .keys = kKeys(kClaim, {})});
        commands_.push_back(Command{.name = std::string{kRelease}, .menu = "Edit", .keys = kKeys(kRelease, {})});
    }
    commands_.push_back(Command{.name = std::string{kFlat}, .menu = "View", .keys = kKeys(kFlat, {})});
    commands_.push_back(Command{.name = std::string{kTilted}, .menu = "View", .keys = kKeys(kTilted, {})});
    for (const auto& [kName, kMode] : kModes) {
        commands_.push_back(Command{.name = std::string{kName}, .menu = "View", .keys = kKeys(kName, {})});
    }
    commands_.push_back(Command{.name = "Pause", .menu = "Game", .keys = kKeys("Pause", {"F6"})});
    commands_.push_back(Command{.name = "Live World", .menu = "Game", .keys = kKeys("Live World", {"F8"})});
    commands_.push_back(Command{.name = "Step", .menu = "Game", .keys = kKeys("Step", {"F7"})});
    for (const auto& [kName, kSize] : kDevices) {
        commands_.push_back(Command{.name = std::string{kName}, .menu = "Game", .keys = kKeys(kName, {})});
    }
    for (const auto& [kName, kCount] : kPlayers) {
        commands_.push_back(Command{.name = std::string{kName}, .menu = "Game", .keys = kKeys(kName, {})});
    }
    for (const auto& [kName, kOverlay] : kOverlays) {
        commands_.push_back(Command{.name = std::string{kName}, .menu = "View", .keys = kKeys(kName, {})});
    }
    for (const auto& [kName, kViews] : kSplits) {
        commands_.push_back(Command{.name = std::string{kName}, .menu = "View", .keys = kKeys(kName, {})});
    }
    // The scene's nine bookmarks (D796), on Control and a digit, kept with
    // Shift too: the keys and the palette, in no menu.
    for (char slot = '1'; slot <= '9'; ++slot) {
        const std::string kGo = std::string{kGoTo} + slot;
        const std::string kSet = std::string{kKeep} + slot;
        commands_.push_back(Command{.name = kGo, .keys = kKeys(kGo, {std::string{"Ctrl+"} + slot})});
        commands_.push_back(Command{.name = kSet, .keys = kKeys(kSet, {std::string{"Ctrl+Shift+"} + slot})});
    }
    // Each command the game's extensions offer, in their menu, by its own
    // name, after all of Studio's: one named as a command Studio has, or
    // as another extension's, is not offered (D874). Then each mode, as
    // its name and "mode", while SPEC-0051's eight modes have room after
    // Studio's own (D877).
    std::size_t modesLeft = kModesLeft;
    for (const ListedExtension& kExtension : extensions_) {
        const auto kOffered = [&](const std::string& name) {
            return std::ranges::none_of(commands_, [&](const Command& command) {
                return command.name == name;
            });
        };
        for (const std::string& kName : kExtension.commands) {
            if (kOffered(kName)) {
                commands_.push_back(Command{.name = kName, .menu = "Extensions", .keys = kKeys(kName, {})});
            }
        }
        for (const std::string& kMode : kExtension.modes) {
            const std::string kName = kMode + " mode";
            if (modesLeft > 0 && kOffered(kName)) {
                --modesLeft;
                commands_.push_back(Command{.name = kName, .menu = "Extensions", .keys = kKeys(kName, {})});
            }
        }
    }
    // Then, as the design's Extensions menu (D938), the game's extensions
    // in the preferences, and read again from its extensions/ folder.
    if (gameOpen()) {
        commands_.push_back(Command{
            .name = std::string{kManageExtensions}, .menu = "Extensions", .keys = kKeys(kManageExtensions, {})});
        commands_.push_back(Command{.name = std::string{kReloadExtensions},
                                    .menu = "Extensions",
                                    .keys = kKeys(kReloadExtensions, {"Ctrl+Alt+R"})});
    }
    // The palette's field, as a field named "commands" is read (D575).
    RAWFRAME_TRY_ASSIGN(const ui::Node kSearch, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kSearch,
                                  ui::Layout{.width = ui::pixels(300),
                                             .height = ui::pixels(24),
                                             .alignItems = ui::Align::Center,
                                             .shrink = 0,
                                             .padding = {28, 8, 0, 0},
                                             .border = {1, 1, 1, 1}}));
    RAWFRAME_TRY(tree_->setLook(kSearch, ui::Look{.fill = look::kBarFill, .borderColor = look::kEdge, .radius = 4}));
    RAWFRAME_TRY(tree_->attach(searchAt.value_or(bar), kSearch));
    RAWFRAME_TRY(words(kSearch, kPaletteHint, look::kFaintInk, look::kLabelSize));
    // Its magnifier, drawn over its start (D915).
    RAWFRAME_TRY_ASSIGN(const ui::Node kGlass, icon(kSearch, Icon::Search, look::kFaintInk, 14));
    RAWFRAME_TRY(tree_->setLayout(kGlass,
                                  ui::Layout{.width = ui::pixels(14),
                                             .height = ui::pixels(14),
                                             .placement = {.absolute = true, .x = ui::pixels(8), .y = ui::pixels(4)}}));
    RAWFRAME_TRY(tree_->setRole(kSearch, ui::Role::TextInput));
    RAWFRAME_TRY(tree_->setName(kSearch, "commands"));
    palette_ = FieldRow{.value = kSearch, .role = FieldRow::Role::Palette, .field = "commands"};
    return {};
}

bool ShellParticipant::runCommand(std::string_view name) {
    const auto kCommand = std::ranges::find(commands_, name, &Command::name);
    if (kCommand == commands_.end()) {
        return false;
    }
    if (kCommand->name == kPalette) {
        openPalette();
        return true;
    }
    if (kCommand->name == kFrame) {
        frameChosen();
        return true;
    }
    if (kCommand->name.starts_with("New ") && kCommand->menu == "Entity") {
        createPart(kCommand->name);
        return true;
    }
    if (kCommand->name == kClaim || kCommand->name == kRelease) {
        claimChosen(kCommand->name == kClaim);
        return true;
    }
    for (const auto& [kPrefix, kGoes] : {std::pair{kGoTo, true}, std::pair{kKeep, false}}) {
        if (kCommand->name.starts_with(kPrefix) && kCommand->name.size() == kPrefix.size() + 1) {
            bookmark(static_cast<std::uint8_t>(kCommand->name.back() - '0'), kGoes);
            return true;
        }
    }
    if (kCommand->name == "Live World") {
        switchLive();
        return true;
    }
    if (kCommand->name == "Pause" || kCommand->name == "Step") {
        runPlayed(kCommand->name == "Step");
        return true;
    }
    if (const auto kDevice = std::ranges::find(
            kDevices, kCommand->name, &std::pair<std::string_view, std::array<std::uint32_t, 2>>::first);
        kDevice != kDevices.end()) {
        device_ = kDevice->second[0] == 0 ? std::nullopt : std::optional{kDevice->second};
        say(std::string{kDevice->first} + (playing_.has_value() ? " from the next play" : ""));
        return true;
    }
    if (const auto kPlayed =
            std::ranges::find(kPlayers, kCommand->name, &std::pair<std::string_view, std::uint32_t>::first);
        kPlayed != kPlayers.end()) {
        players_ = kPlayed->second;
        say(std::to_string(players_) + (players_ == 1 ? " player" : " players") +
            (playing_.has_value() ? " from the next play" : ""));
        return true;
    }
    if (const auto kOverlay =
            std::ranges::find(kOverlays, kCommand->name, &std::pair<std::string_view, std::string_view>::first);
        kOverlay != kOverlays.end()) {
        const std::string kName{kOverlay->second};
        const bool kOn = !std::ranges::contains(shown_.overlays, kName);
        if (kOn) {
            shown_.overlays.push_back(kName);
        } else {
            std::erase(shown_.overlays, kName);
        }
        tellOverlays();
        showViewBar();
        say(kName + (kOn ? " shown" : " hidden"));
        return true;
    }
    if (const auto kSplit =
            std::ranges::find(kSplits, kCommand->name, &std::pair<std::string_view, std::uint8_t>::first);
        kSplit != kSplits.end()) {
        shown_.views = kSplit->second;
        tellSplit();
        say(std::to_string(shown_.views) + (shown_.views == 1 ? " view" : " views"));
        return true;
    }
    if (kCommand->name == kFlat || kCommand->name == kTilted) {
        seeIn2D(kCommand->name == kFlat);
        return true;
    }
    if (const auto kMode =
            std::ranges::find(kModes, kCommand->name, &std::pair<std::string_view, std::string_view>::first);
        kMode != kModes.end()) {
        shown_.viewMode = std::string{kMode->second};
        tellViewMode();
        showViewBar();
        say(shown_.viewMode + " view");
        return true;
    }
    if (kCommand->name == kSaveLayout) {
        saveLayout();
        return true;
    }
    if (kCommand->menu == "Window" && kCommand->name.ends_with(kLayoutSuffix)) {
        const std::vector<Layout> kLayoutsMade = layouts();
        const auto kChosenLayout =
            std::ranges::find(kLayoutsMade,
                              std::string_view{kCommand->name}.substr(0, kCommand->name.size() - kLayoutSuffix.size()),
                              &Layout::name);
        if (kChosenLayout != kLayoutsMade.end()) {
            chooseLayout(*kChosenLayout);
        }
        return true;
    }
    if (kCommand->name == kShortcuts || kCommand->name == kAbout) {
        closeExport();
        closePreferences();
        held_.preferencePage = kCommand->name == kShortcuts ? EditorsHeld::kKeysPage : EditorsHeld::kAboutPage;
        openPreferences();
        say(kCommand->name == kShortcuts ? "keyboard shortcuts" : "about Rawframe Studio");
        return true;
    }
    if (kCommand->name == kPreferences) {
        closeExport();
        openPreferences();
        say(dialog_.has_value() ? "preferences" : "preferences closed");
        return true;
    }
    if (kCommand->menu == "File" && kCommand->name != kExport) {
        if (kCommand->name == "New game" || kCommand->name == "Close game") {
            closeGame();
        } else if (kCommand->name == "Open game") {
            askDialog(view::FileDialogs::Kind::Folder, "Open a game's folder", "open");
        } else if (kCommand->name == "Save") {
            say("saved: Studio writes each change to the game's files as it is made");
        } else if (kCommand->name == "Import files") {
            importAsked();
        } else if (kCommand->name == "Quit") {
            if (closing_ != nullptr) {
                closing_->ask();
            } else {
                say("this window cannot be closed from here");
            }
        }
        return true;
    }
    if (kCommand->name == kExport) {
        openExport();
        say(held_.exportDialog.has_value() ? "export" : "export closed");
        return true;
    }
    if (kCommand->name == kManageExtensions) {
        closeExport();
        closePreferences();
        held_.preferencePage = EditorsHeld::kExtensionsPage;
        openPreferences();
        say("extensions");
        return true;
    }
    if (kCommand->name == kReloadExtensions) {
        extensions_ = extensionsOf(ask(extensionsRecord(next())));
        rebuild();
        say(std::to_string(extensions_.size()) + (extensions_.size() == 1 ? " extension" : " extensions") +
            " read again");
        return true;
    }
    if (kCommand->menu == "Extensions") {
        if (kCommand->name.ends_with(" mode")) {
            switchExtensionMode(kCommand->name.substr(0, kCommand->name.size() - 5));
        } else {
            runExtensionCommand(kCommand->name);
        }
        return true;
    }
    if (!tree_->contains(kCommand->button)) {
        return false;
    }
    pressOn(kCommand->button, false);
    return true;
}

void ShellParticipant::runChord(std::string_view chord) {
    for (const Command& kCommand : commands_) {
        if (std::ranges::find(kCommand.keys, chord) != kCommand.keys.end()) {
            // A copy: the command may rebuild what lists it.
            const std::string kName = kCommand.name;
            closePopup();
            static_cast<void>(runCommand(kName));
            return;
        }
    }
}

bool ShellParticipant::commandOn(std::string_view name) const {
    // What a pill's row stands for that is on now (D916).
    if (const auto kOverlay = std::ranges::find(kOverlays, name, &std::pair<std::string_view, std::string_view>::first);
        kOverlay != kOverlays.end()) {
        return std::ranges::contains(shown_.overlays, std::string{kOverlay->second});
    }
    if (const auto kMode = std::ranges::find(kModes, name, &std::pair<std::string_view, std::string_view>::first);
        kMode != kModes.end()) {
        return shown_.viewMode == kMode->second;
    }
    if (const auto kSplit = std::ranges::find(kSplits, name, &std::pair<std::string_view, std::uint8_t>::first);
        kSplit != kSplits.end()) {
        return shown_.views == kSplit->second;
    }
    return (name == kFlat && held_.flat) || (name == kTilted && !held_.flat);
}

result::Result<ui::Node>
ShellParticipant::menuLine(ui::Node popup, const Command& command, bool lit, std::optional<bool> checked) {
    // A line as the design's menus (D923): its name, and its key at the
    // end in the fixed-width face; lit where it is on, or the first match.
    RAWFRAME_TRY_ASSIGN(const ui::Node kLine,
                        box(popup,
                            ui::Layout{.height = ui::pixels(28),
                                       .alignItems = ui::Align::Center,
                                       .gap = 12,
                                       .shrink = 0,
                                       .padding = {10, 10, 0, 0}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kLine, ui::Look{.fill = lit ? kChosen : 0U, .radius = 4}));
    // A checkbox first where it says what is on (D927).
    if (checked.has_value()) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kCheck,
                            box(kLine,
                                ui::Layout{.width = ui::pixels(14),
                                           .height = ui::pixels(14),
                                           .justify = ui::Justify::Center,
                                           .alignItems = ui::Align::Center,
                                           .shrink = 0,
                                           .border = {1, 1, 1, 1}},
                                0));
        RAWFRAME_TRY(tree_->setLook(kCheck,
                                    ui::Look{.fill = *checked ? look::kAccent : 0U,
                                             .borderColor = *checked ? look::kAccent : look::kEdge,
                                             .radius = 3}));
        RAWFRAME_TRY(tree_->setInteraction(kCheck, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        if (*checked) {
            RAWFRAME_TRY(icon(kCheck, Icon::Check, look::kInk, 11));
        }
    }
    RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kLine, ui::Layout{.width = ui::pixels(0), .grow = 1}, 0));
    RAWFRAME_TRY(words(kName, command.name, look::kRowInk, look::kBodySize));
    RAWFRAME_TRY(tree_->setInteraction(kName, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    if (!command.keys.empty()) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kKey, box(kLine, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(monoWords(kKey,
                               command.keys.front() == "Delete" ? std::string{"Del"} : command.keys.front(),
                               look::kFaintInk,
                               look::kSmallSize));
        RAWFRAME_TRY(tree_->setInteraction(kKey, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    }
    RAWFRAME_TRY(tree_->setRole(kLine, ui::Role::Button));
    RAWFRAME_TRY(tree_->setName(kLine, lineOf(command)));
    return kLine;
}

void ShellParticipant::showMenu(std::size_t at) {
    const bool kWasOpen = menuOpen_ == at;
    closePopup();
    if (kWasOpen || at >= menus_.size()) {
        return;
    }
    auto popup = box(root_,
                     ui::Layout{.width = ui::pixels(260),
                                .direction = ui::Direction::Column,
                                .gap = 1,
                                .padding = {5, 5, 5, 5},
                                .border = {1, 1, 1, 1},
                                .placement = {.absolute = true}},
                     0);
    if (popup.has_value()) {
        static_cast<void>(tree_->setLook(*popup, kMenuLook));
    }
    if (!popup.has_value() || !tree_
                                   ->setPopup(*popup,
                                              ui::Popup{.anchor = menus_[at].first,
                                                        .side = ui::Popup::Side::Below,
                                                        .gap = 2,
                                                        .margin = 4,
                                                        .align = ui::Popup::Align::Start})
                                   .has_value()) {
        return;
    }
    static_cast<void>(tree_->setRole(*popup, ui::Role::Group));
    static_cast<void>(tree_->setName(*popup, menus_[at].second));
    popup_ = *popup;
    menuOpen_ = at;
    // The layout chooser lists the Window menu's layouts (D911).
    const bool kChooser = menus_[at].second == kLayoutChooser;
    for (std::size_t each = 0; each < commands_.size(); ++each) {
        const Command& kCommand = commands_[each];
        // A viewport's pill lists its own commands (D916).
        const bool kPill = isPill(menus_[at].second);
        if (kPill      ? !pillLists(menus_[at].second, kCommand.name)
            : kChooser ? kCommand.menu != "Window" || !kCommand.name.ends_with(kLayoutSuffix)
                       : kCommand.menu != menus_[at].second) {
            continue;
        }
        if (!kPill && !kChooser && !popupRows_.empty() && std::ranges::contains(kGroupsBegun, kCommand.name)) {
            auto rule = box(
                *popup, ui::Layout{.height = ui::pixels(1), .shrink = 0, .margin = {4, 4, 3, 3}}, look::kRaisedEdge);
            if (!rule.has_value()) {
                return;
            }
        }
        // The Show pill's rows each a checkbox, as the design's overlays
        // menu (D927); the other pills' row that is on lit.
        const bool kChecked = menus_[at].second == "Show";
        auto line = menuLine(*popup,
                             kCommand,
                             !kChecked && kPill && commandOn(kCommand.name),
                             kChecked ? std::optional<bool>{commandOn(kCommand.name)} : std::nullopt);
        if (!line.has_value()) {
            return;
        }
        popupRows_.emplace_back(*line, each);
    }
}

void ShellParticipant::showMatches(std::string_view typed) {
    closePopup();
    if (!palette_.has_value()) {
        return;
    }
    auto popup = box(root_,
                     ui::Layout{.width = ui::pixels(380),
                                .direction = ui::Direction::Column,
                                .gap = 1,
                                .padding = {5, 5, 5, 5},
                                .border = {1, 1, 1, 1},
                                .placement = {.absolute = true}},
                     0);
    if (popup.has_value()) {
        static_cast<void>(tree_->setLook(*popup, kMenuLook));
    }
    if (!popup.has_value() ||
        !tree_
             ->setPopup(*popup,
                        ui::Popup{.anchor = palette_->value, .side = ui::Popup::Side::Below, .gap = 2, .margin = 4})
             .has_value()) {
        return;
    }
    static_cast<void>(tree_->setRole(*popup, ui::Role::Group));
    static_cast<void>(tree_->setName(*popup, "matches"));
    popup_ = *popup;
    // `>` narrows to commands and `@` to the scene's entities, as the
    // design's palette says at its foot (D937).
    const bool kCommandsOnly = typed.starts_with('>');
    const bool kEntitiesOnly = typed.starts_with('@');
    const std::string kWanted = lowered(kCommandsOnly || kEntitiesOnly ? typed.substr(1) : typed);
    // As the design's palette (D927): the commands, then with words typed
    // the open scene's entities and the game's assets they name, each
    // under its heading.
    const std::size_t kMostCommands = kWanted.empty() ? 12 : 7;
    const auto kHeading = [this, &popup](std::string_view text) {
        if (auto heading = box(*popup, ui::Layout{.shrink = 0, .padding = {10, 10, 8, 3}}, 0); heading.has_value()) {
            static_cast<void>(monoWords(*heading, text, look::kFaintInk, look::kSmallSize));
        }
    };
    const auto kFound = [this, &popup](std::string_view name,
                                       std::string_view quiet,
                                       Icon which,
                                       std::uint32_t ink,
                                       std::size_t code) {
        auto line = box(*popup,
                        ui::Layout{.height = ui::pixels(28),
                                   .alignItems = ui::Align::Center,
                                   .gap = 8,
                                   .shrink = 0,
                                   .padding = {10, 10, 0, 0}},
                        0);
        if (!line.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*line, ui::Look{.fill = popupRows_.empty() ? kChosen : 0U, .radius = 4}));
        static_cast<void>(icon(*line, which, ink, 14));
        if (auto named = box(*line, ui::Layout{.width = ui::pixels(0), .grow = 1}, 0); named.has_value()) {
            static_cast<void>(tree_->setLook(*named, ui::Look{.clip = true}));
            static_cast<void>(words(*named, name, look::kRowInk, look::kBodySize));
            static_cast<void>(tree_->setInteraction(*named, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        if (auto said = box(*line, ui::Layout{.shrink = 0}, 0); said.has_value()) {
            static_cast<void>(monoWords(*said, quiet, look::kFaintInk, look::kSmallSize));
            static_cast<void>(tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        static_cast<void>(tree_->setRole(*line, ui::Role::Button));
        static_cast<void>(tree_->setName(*line, std::string{name}));
        popupRows_.emplace_back(*line, code);
    };
    bool headed = false;
    for (std::size_t each = 0; each < commands_.size() && popupRows_.size() < kMostCommands && !kEntitiesOnly; ++each) {
        if (commands_[each].name == kPalette || !lowered(commands_[each].name).contains(kWanted)) {
            continue;
        }
        if (!kWanted.empty() && !std::exchange(headed, true)) {
            kHeading("COMMANDS");
        }
        auto line = menuLine(*popup, commands_[each], popupRows_.empty());
        if (!line.has_value()) {
            return;
        }
        // A command with no key says the menu it is in.
        if (commands_[each].keys.empty() && !commands_[each].menu.empty()) {
            if (auto menu = box(*line, ui::Layout{.shrink = 0}, 0); menu.has_value()) {
                static_cast<void>(words(*menu, commands_[each].menu, look::kFaintInk, look::kSmallSize));
                static_cast<void>(
                    tree_->setInteraction(*menu, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
        }
        popupRows_.emplace_back(*line, each);
    }
    const auto kFoot = [this, &popup] {
        if (auto foot = box(
                *popup,
                ui::Layout{
                    .gap = 14, .shrink = 0, .padding = {10, 10, 7, 4}, .margin = {0, 0, 4, 0}, .border = {0, 0, 1, 0}},
                0);
            foot.has_value()) {
            static_cast<void>(tree_->setLook(*foot, ui::Look{.borderColor = look::kRaisedEdge}));
            for (const std::string_view kHint :
                 {"Enter runs the first", "> commands only", "@ entities only", "Esc closes"}) {
                if (auto hint = box(*foot, ui::Layout{.shrink = 0}, 0); hint.has_value()) {
                    static_cast<void>(words(*hint, kHint, look::kFaintInk, look::kSmallSize));
                }
            }
        }
    };
    if (kWanted.empty() && !kEntitiesOnly) {
        kFoot();
        return;
    }
    headed = false;
    std::size_t entities = 0;
    for (std::size_t each = 0; each < entities_.size() && !documentChosen() && entities < 5 && !kCommandsOnly; ++each) {
        if (each >= names_.size() || names_[each].empty() || !lowered(names_[each]).contains(kWanted)) {
            continue;
        }
        if (!std::exchange(headed, true)) {
            std::string scene = std::filesystem::path{scene_}.filename().string();
            std::ranges::transform(scene, scene.begin(), [](char letter) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
            });
            kHeading("ENTITIES IN " + scene);
        }
        kFound(names_[each], "entity", Icon::Cube, look::kEntityInk, kFoundEntity + each);
        ++entities;
    }
    headed = false;
    std::size_t assets = 0;
    for (std::size_t each = 0; each < browser_.sources.size() && assets < 5 && !kCommandsOnly && !kEntitiesOnly;
         ++each) {
        if (!lowered(browser_.sources[each].path).contains(kWanted)) {
            continue;
        }
        if (!std::exchange(headed, true)) {
            kHeading("ASSETS");
        }
        const auto [kIcon, kInk] = kindIcon(kindOf(browser_.sources[each].importer));
        kFound(browser_.sources[each].path,
               std::filesystem::path{browser_.sources[each].path}.extension().string().substr(
                   std::filesystem::path{browser_.sources[each].path}.has_extension() ? 1 : 0),
               kIcon,
               kInk,
               kFoundAsset + each);
        ++assets;
    }
    kFoot();
}

void ShellParticipant::runFound(std::size_t code) {
    if (code >= kFoundAsset) {
        // An asset: its document opened where it is one, else chosen in
        // Assets.
        const std::size_t kAt = code - kFoundAsset;
        if (kAt >= browser_.sources.size()) {
            return;
        }
        const std::string kPath = browser_.sources[kAt].path;
        if (const auto kOpened = std::ranges::find(scenes_, kPath); kOpened != scenes_.end()) {
            showScene(static_cast<std::size_t>(kOpened - scenes_.begin()));
        } else {
            showDock(Dock::Assets);
            say(kPath + " is in Assets");
        }
        return;
    }
    if (code >= kFoundEntity && code - kFoundEntity < entities_.size()) {
        chooseEntity(code - kFoundEntity);
    }
}

void ShellParticipant::paletteTyped(std::optional<view::TypingKey> left) {
    if (left == view::TypingKey::Submit || left == view::TypingKey::Dismiss) {
        const std::optional<std::size_t> kFirst = left == view::TypingKey::Submit && !popupRows_.empty()
                                                      ? std::optional{popupRows_.front().second}
                                                      : std::nullopt;
        endEdit();
        static_cast<void>(words(palette_->value, kPaletteHint, look::kFaintInk, look::kLabelSize));
        closePopup();
        if (kFirst.has_value() && *kFirst >= kFoundEntity) {
            runFound(*kFirst);
        } else if (kFirst.has_value()) {
            const std::string kName = commands_[*kFirst].name;
            static_cast<void>(runCommand(kName));
        }
        return;
    }
    showMatches(tree_->textOf(palette_->value));
}

void ShellParticipant::openPalette() {
    if (!palette_.has_value()) {
        return;
    }
    static_cast<void>(words(palette_->value, "", kText, 14));
    beginEdit(*palette_);
    showMatches({});
}

void ShellParticipant::closePalette() {
    if (editing_.has_value() && editing_->role == FieldRow::Role::Palette) {
        endEdit();
    }
    if (palette_.has_value()) {
        static_cast<void>(words(palette_->value, kPaletteHint, look::kFaintInk, look::kLabelSize));
    }
}

void ShellParticipant::closePopup() {
    if (popup_.has_value() && tree_->contains(*popup_)) {
        static_cast<void>(tree_->remove(*popup_));
    }
    popup_.reset();
    menuOpen_.reset();
    popupRows_.clear();
    choiceRows_.clear();
    choosing_.reset();
}

void ShellParticipant::showChoices(const FieldRow& edited, const std::vector<std::string>& choices) {
    closePopup();
    auto popup = box(root_,
                     ui::Layout{.width = ui::pixels(200),
                                .direction = ui::Direction::Column,
                                .gap = 2,
                                .padding = {4, 4, 4, 4},
                                .placement = {.absolute = true}},
                     kHeaderFill);
    if (!popup.has_value() ||
        !tree_
             ->setPopup(*popup,
                        ui::Popup{.anchor = edited.value, .side = ui::Popup::Side::Below, .gap = 2, .margin = 4})
             .has_value()) {
        return;
    }
    static_cast<void>(tree_->setRole(*popup, ui::Role::Group));
    static_cast<void>(tree_->setName(*popup, edited.field + " choices"));
    popup_ = *popup;
    choosing_ = edited;
    // The value it has now marked.
    const std::string kNow{tree_->textOf(edited.value)};
    for (const std::string& kChoice : choices) {
        auto line = row(*popup, kChoice, kText, kChoice == kNow ? kChosen : kRow, true);
        if (!line.has_value()) {
            return;
        }
        choiceRows_.emplace_back(*line, kChoice);
    }
}

} // namespace rawframe::studio
