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
constexpr std::string_view kMenus[] = {"File", "Edit", "Entity", "Game", "Extensions", "View", "Window"};

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
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kOverlays = {
    {{"Show grid", "grid"}, {"Show colliders", "colliders"}, {"Show lights", "lights"}}};

/// The preview split one, two, or three ways (D797).
constexpr std::array<std::pair<std::string_view, std::uint8_t>, 3> kSplits = {
    {{"One view", 1}, {"Two views", 2}, {"Three views", 3}}};

/// A bookmark's commands, each followed by its slot (D796).
constexpr std::string_view kGoTo = "Go to bookmark ";
constexpr std::string_view kKeep = "Set bookmark ";

/// How the preview draws (D794), each by the renderer's name for it.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kModes = {
    {{"Lit", "lit"}, {"Unlit", "unlit"}, {"Normals", "normals"}}};

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
    kOffer("Assets", "Window", {}, assetsTab_);
    kOffer("History", "Window", {}, historyTab_);
    kOffer("Console", "Window", {}, consoleTab_);
    kOffer("Network", "Window", {}, networkTab_);
    kOffer("Environment", "Window", {}, environmentTab_);
    kOffer("Profiler", "Window", {}, profilerTab_);
    kOffer("Frame", "Window", {}, held_.frameTab);
    kOffer("Changes", "Window", {}, held_.changesTab);
    kOffer("Extension panels", "Window", {}, held_.panelsTab);
    // The menus whose commands come below, with no button, always shown.
    for (const std::string_view kMenu : kMenus) {
        if (std::ranges::none_of(commands_,
                                 [&](const Command& command) {
                                     return command.menu == kMenu;
                                 }) &&
            kMenu != "File" && kMenu != "Window" && kMenu != "View" &&
            (kMenu != "Extensions" || std::ranges::all_of(extensions_, [](const ListedExtension& extension) {
                 return extension.commands.empty() && extension.modes.empty();
             }))) {
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
    // The layouts, where the surface table places "Docks and saved
    // layouts".
    for (const Layout& layout : layouts()) {
        const std::string kName = layout.name + std::string{kLayoutSuffix};
        commands_.push_back(Command{.name = kName, .menu = "Window", .keys = kKeys(kName, {})});
    }
    commands_.push_back(Command{.name = std::string{kSaveLayout}, .menu = "Window", .keys = kKeys(kSaveLayout, {})});
    commands_.push_back(Command{.name = std::string{kExport}, .menu = "File", .keys = kKeys(kExport, {})});
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
    if (kCommand->name == kPreferences) {
        closeExport();
        openPreferences();
        say(dialog_.has_value() ? "preferences" : "preferences closed");
        return true;
    }
    if (kCommand->name == kExport) {
        openExport();
        say(held_.exportDialog.has_value() ? "export" : "export closed");
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

void ShellParticipant::showMenu(std::size_t at) {
    const bool kWasOpen = menuOpen_ == at;
    closePopup();
    if (kWasOpen || at >= menus_.size()) {
        return;
    }
    auto popup = box(root_,
                     ui::Layout{.width = ui::pixels(240),
                                .direction = ui::Direction::Column,
                                .gap = 2,
                                .padding = {4, 4, 4, 4},
                                .placement = {.absolute = true}},
                     kHeaderFill);
    if (!popup.has_value() ||
        !tree_
             ->setPopup(*popup,
                        ui::Popup{.anchor = menus_[at].first, .side = ui::Popup::Side::Below, .gap = 2, .margin = 4})
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
        if (kPill ? !pillLists(menus_[at].second, kCommand.name)
            : kChooser ? kCommand.menu != "Window" || !kCommand.name.ends_with(kLayoutSuffix)
                       : kCommand.menu != menus_[at].second) {
            continue;
        }
        auto line = row(*popup, lineOf(kCommand), kText, kPill && commandOn(kCommand.name) ? kChosen : kRow, true);
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
                     ui::Layout{.width = ui::pixels(260),
                                .direction = ui::Direction::Column,
                                .gap = 2,
                                .padding = {4, 4, 4, 4},
                                .placement = {.absolute = true}},
                     kHeaderFill);
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
    const std::string kWanted = lowered(typed);
    for (std::size_t each = 0; each < commands_.size() && popupRows_.size() < 12; ++each) {
        if (commands_[each].name == kPalette || !lowered(commands_[each].name).contains(kWanted)) {
            continue;
        }
        auto line = row(*popup, lineOf(commands_[each]), kText, popupRows_.empty() ? kChosen : kRow, true);
        if (!line.has_value()) {
            return;
        }
        popupRows_.emplace_back(*line, each);
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
        if (kFirst.has_value()) {
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
