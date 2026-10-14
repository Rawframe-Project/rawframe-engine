#include "shell.h"

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
constexpr std::string_view kMenus[] = {"Edit", "Entity", "Game", "Window"};

/// The palette's own command, which has no button: it gives the palette's
/// field the keyboard.
constexpr std::string_view kPalette = "Command palette";

/// What the palette's field reads while it does not hold the keyboard.
constexpr std::string_view kPaletteHint = "commands  Ctrl+K";

} // namespace

result::Status ShellParticipant::showMenus(ui::Node bar) {
    commands_.clear();
    menus_.clear();
    // Only what a button does now: an operation the catalog does not offer
    // has no button, and so no command (D777).
    const auto kOffer =
        [&](std::string_view name, std::string_view menu, std::vector<std::string> keys, ui::Node button) {
            if (tree_->contains(button)) {
                commands_.push_back(Command{
                    .name = std::string{name}, .menu = std::string{menu}, .keys = std::move(keys), .button = button});
            }
        };
    kOffer("Cook", "Game", {"Ctrl+B"}, cookNode_);
    kOffer("Undo", "Edit", {"Ctrl+Z"}, undoNode_);
    kOffer("Redo", "Edit", {"Ctrl+Y", "Ctrl+Shift+Z"}, redoNode_);
    kOffer("Duplicate", "Edit", {"Ctrl+D"}, duplicateNode_);
    kOffer("Delete", "Edit", {"Delete"}, deleteNode_);
    kOffer("New", "Entity", {"Ctrl+Shift+N"}, newNode_);
    kOffer("Move to", "Entity", {}, moveNode_);
    kOffer("Restore", "Entity", {}, restoreNode_);
    kOffer("Remove instance", "Entity", {}, uninstanceNode_);
    kOffer("Up", "Entity", {}, upNode_);
    kOffer("Down", "Entity", {}, downNode_);
    kOffer("Play", "Game", {"F5"}, playNode_);
    kOffer("Assets", "Window", {}, assetsTab_);
    kOffer("History", "Window", {}, historyTab_);
    for (const std::string_view kMenu : kMenus) {
        if (std::ranges::none_of(commands_,
                                 [&](const Command& command) {
                                     return command.menu == kMenu;
                                 }) &&
            kMenu != "Window") {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kButton, box(bar, ui::Layout{.shrink = 0, .padding = {8, 3, 8, 3}}, 0));
        RAWFRAME_TRY(pressWords(kButton, kMenu, kText, 14));
        menus_.emplace_back(kButton, std::string{kMenu});
    }
    commands_.push_back(Command{.name = std::string{kPalette}, .menu = "Window", .keys = {"Ctrl+K"}});
    // The palette's field, as a field named "commands" is read (D575).
    RAWFRAME_TRY_ASSIGN(const ui::Node kSearch, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(
        kSearch, ui::Layout{.width = ui::pixels(220), .height = ui::pixels(22), .shrink = 0, .padding = {6, 2, 6, 2}}));
    RAWFRAME_TRY(tree_->setLook(kSearch, ui::Look{.fill = kField, .radius = 3}));
    RAWFRAME_TRY(tree_->attach(bar, kSearch));
    RAWFRAME_TRY(words(kSearch, kPaletteHint, kQuiet, 14));
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
    for (std::size_t each = 0; each < commands_.size(); ++each) {
        if (commands_[each].menu != menus_[at].second) {
            continue;
        }
        auto line = row(*popup, lineOf(commands_[each]), kText, kRow, true);
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
        static_cast<void>(words(palette_->value, kPaletteHint, kQuiet, 14));
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
        static_cast<void>(words(palette_->value, kPaletteHint, kQuiet, 14));
    }
}

void ShellParticipant::closePopup() {
    if (popup_.has_value() && tree_->contains(*popup_)) {
        static_cast<void>(tree_->remove(*popup_));
    }
    popup_.reset();
    menuOpen_.reset();
    popupRows_.clear();
}

} // namespace rawframe::studio
