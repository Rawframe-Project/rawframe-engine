#include "shell.h"

#include <algorithm>
#include <array>

namespace rawframe::studio {

namespace {

/// Why typed keys are refused: the chords the window names.
constexpr std::string_view kChordsAre = "Ctrl, Shift, Alt, then F1 to F12, a letter or digit with Ctrl, or Delete";

} // namespace

void ShellParticipant::openPreferences() {
    if (dialog_.has_value()) {
        closePreferences();
        return;
    }
    closePopup();
    // As the design's dialog (D923): over the window dimmed, a card with
    // its title and close, its pages on the left, the page chosen, and
    // where it is kept.
    auto backdrop = box(root_,
                        ui::Layout{.width = ui::share(1),
                                   .height = ui::share(1),
                                   .justify = ui::Justify::Center,
                                   .alignItems = ui::Align::Center,
                                   .placement = {.absolute = true, .x = ui::pixels(0), .y = ui::pixels(0)}},
                        0);
    if (!backdrop.has_value()) {
        return;
    }
    dialog_ = *backdrop;
    static_cast<void>(tree_->setLook(*backdrop, ui::Look{.fill = 0x00000088U}));
    auto card = box(*backdrop,
                    ui::Layout{.width = ui::pixels(760),
                               .height = ui::share(0.78F),
                               .direction = ui::Direction::Column,
                               .border = {1, 1, 1, 1}},
                    0);
    if (!card.has_value()) {
        return;
    }
    static_cast<void>(tree_->setLook(*card,
                                     ui::Look{.fill = look::kPanelFill,
                                              .borderColor = look::kRaisedEdge,
                                              .radius = 8,
                                              .clip = true,
                                              .outerShadow = {.color = 0x00000099U, .y = 10, .blur = 30}}));
    static_cast<void>(tree_->setRole(*card, ui::Role::Group));
    // Not "Preferences", the command's name, which a screen reader finds
    // in the Window menu (D777).
    static_cast<void>(tree_->setName(*card, "Studio preferences"));
    shortcutFields_.clear();
    editorField_.reset();
    held_.preferencePages.clear();
    if (auto head = box(*card,
                        ui::Layout{.height = ui::pixels(46),
                                   .alignItems = ui::Align::Center,
                                   .shrink = 0,
                                   .padding = {16, 10, 0, 0},
                                   .border = {0, 0, 0, 1}},
                        0);
        head.has_value()) {
        static_cast<void>(tree_->setLook(*head, ui::Look{.borderColor = look::kLine}));
        if (auto title = box(*head, ui::Layout{.width = ui::pixels(0), .grow = 1}, 0); title.has_value()) {
            static_cast<void>(strongWords(*title, "Preferences", look::kInk, 15));
        }
        if (auto close = iconButton(*head, Icon::Close, "Close preferences"); close.has_value()) {
            held_.preferencesX = *close;
        }
    }
    auto body = box(*card, ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(0), .grow = 1}, 0);
    if (!body.has_value()) {
        return;
    }
    if (auto pages = box(*body,
                         ui::Layout{.width = ui::pixels(180),
                                    .direction = ui::Direction::Column,
                                    .gap = 2,
                                    .shrink = 0,
                                    .padding = {8, 8, 12, 8},
                                    .border = {0, 1, 0, 0}},
                         0);
        pages.has_value()) {
        static_cast<void>(tree_->setLook(*pages, ui::Look{.borderColor = look::kLine}));
        constexpr std::array<std::string_view, 4> kPages = {"General", "Keys", "Agent and MCP", "Extensions"};
        for (std::size_t each = 0; each < kPages.size(); ++each) {
            auto page = box(
                *pages,
                ui::Layout{
                    .height = ui::pixels(30), .alignItems = ui::Align::Center, .shrink = 0, .padding = {10, 10, 0, 0}},
                0);
            if (!page.has_value()) {
                continue;
            }
            static_cast<void>(
                tree_->setLook(*page, ui::Look{.fill = each == held_.preferencePage ? kChosen : 0U, .radius = 4}));
            if (auto said = box(*page, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                static_cast<void>(words(
                    *said, kPages[each], each == held_.preferencePage ? look::kInk : look::kSoftInk, look::kBodySize));
                static_cast<void>(
                    tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
            static_cast<void>(tree_->setRole(*page, ui::Role::Button));
            static_cast<void>(tree_->setName(*page, kPages[each]));
            held_.preferencePages.emplace_back(*page, each);
        }
    }
    auto content = box(*body,
                       ui::Layout{.width = ui::pixels(0),
                                  .minHeight = ui::pixels(0),
                                  .direction = ui::Direction::Column,
                                  .gap = 4,
                                  .grow = 1,
                                  .padding = {16, 16, 14, 14},
                                  .scroll = ui::Scroll::Vertical},
                       0);
    if (!content.has_value()) {
        return;
    }
    const ui::Node kContent = *content;
    const auto kHeading = [this, kContent](std::string_view title, std::string_view quiet) {
        if (auto heading =
                box(kContent,
                    ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {0, 0, 8, 6}},
                    0);
            heading.has_value()) {
            if (auto titled = box(*heading, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
                static_cast<void>(strongWords(*titled, title, look::kInk, 14));
            }
            if (!quiet.empty()) {
                if (auto said = box(*heading, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                    static_cast<void>(wrappedWords(*said, quiet, look::kFaintInk, look::kLabelSize));
                }
            }
        }
    };
    const auto kMono = [this, kContent](std::string_view text) {
        if (auto well = box(kContent, ui::Layout{.shrink = 0, .padding = {10, 10, 7, 7}, .border = {1, 1, 1, 1}}, 0);
            well.has_value()) {
            static_cast<void>(
                tree_->setLook(*well, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 5}));
            static_cast<void>(monoWords(*well, text, look::kSoftInk, look::kSmallSize));
        }
    };
    if (held_.preferencePage == 0) {
        kHeading("Editor", "The program a script or a file opens in, {file}, {line}, and {column} where it goes.");
        if (auto line = fieldRow("editor", editor_, kContent, !preferences_.editor.empty(), "Command");
            line.has_value()) {
            editorField_ = FieldRow{.value = line->second, .role = FieldRow::Role::Editor, .field = "editor"};
        }
        kHeading("New games", "Where the start screen makes a new game unless its folder is changed there.");
        kMono(projects_.string());
    } else if (held_.preferencePage == 1) {
        // Each command's keys, brighter where the author gave them; a chord
        // two commands share is said under each, the first in the list
        // taking it (D782).
        kHeading("Keys", "Each command's keys, typed as Ctrl+B or F5, apart by commas; empty for none.");
        for (const Command& kCommand : commands_) {
            auto line = fieldRow(kCommand.name + " keys",
                                 chordsText(kCommand.keys),
                                 kContent,
                                 preferences_.keys.contains(kCommand.name),
                                 kCommand.name);
            if (!line.has_value()) {
                continue;
            }
            shortcutFields_.push_back(
                FieldRow{.value = line->second, .role = FieldRow::Role::Shortcut, .field = kCommand.name});
            for (const std::string& kChord : kCommand.keys) {
                for (const Command& kOther : commands_) {
                    if (&kOther != &kCommand && std::ranges::find(kOther.keys, kChord) != kOther.keys.end()) {
                        static_cast<void>(row(kContent, kChord + " is also " + kOther.name + "'s", kWarning, 0U));
                    }
                }
            }
        }
    } else if (held_.preferencePage == 2) {
        kHeading("Agent and MCP",
                 "An agent edits the game with the same tools Studio uses, without Studio. Give it this command; "
                 "each change it makes shows in History with its name, and Undo takes it back.");
        kMono("rawframe-author mcp " + description_.string());
        kHeading("Together", "Two authors on one game: File, then the game's host, as rawframe-author host and join.");
    } else {
        // The game's extensions (SPEC-0051's Preferences: Extensions,
        // D874): each by its path, what its doors are of, and its
        // commands, or why it offers none.
        kHeading("Extensions",
                 "Kest programs under the game's extensions/ folder. They run as trusted code, so trust only what "
                 "you have read.");
        if (extensions_.empty()) {
            static_cast<void>(row(kContent, "None in this game.", kQuiet, 0U));
        }
        for (const ListedExtension& kExtension : extensions_) {
            std::string doors;
            for (const std::string& kCapability : kExtension.capabilities) {
                doors += (doors.empty() ? "" : ", ") + kCapability;
            }
            kMono(kExtension.path + "  doors: " + (doors.empty() ? "none" : doors));
            if (!kExtension.refused.empty()) {
                static_cast<void>(row(kContent, kExtension.refused, kWarning, 0U));
            }
            for (const std::string& kCommand : kExtension.commands) {
                static_cast<void>(row(kContent, "  " + kCommand, kQuiet, 0U));
            }
        }
    }
    if (auto foot = box(*card,
                        ui::Layout{.height = ui::pixels(50),
                                   .alignItems = ui::Align::Center,
                                   .shrink = 0,
                                   .padding = {16, 12, 0, 0},
                                   .border = {0, 0, 1, 0}},
                        0);
        foot.has_value()) {
        static_cast<void>(tree_->setLook(*foot, ui::Look{.borderColor = look::kLine}));
        if (auto said = box(*foot, ui::Layout{.width = ui::pixels(0), .grow = 1}, 0); said.has_value()) {
            static_cast<void>(
                words(*said, "Saved to your user folder, not the game", look::kFaintInk, look::kLabelSize));
        }
        if (auto close = textButton(*foot, std::nullopt, "Close"); close.has_value()) {
            dialogClose_ = *close;
        }
    }
}

void ShellParticipant::closePreferences() {
    if (!dialog_.has_value()) {
        return;
    }
    endEdit();
    if (tree_->contains(*dialog_)) {
        static_cast<void>(tree_->remove(*dialog_));
    }
    dialog_.reset();
    shortcutFields_.clear();
    editorField_.reset();
}

bool ShellParticipant::pressPreferences(ui::Node node) {
    if (!dialog_.has_value()) {
        return false;
    }
    if (node == dialogClose_ || (node == held_.preferencesX && node != ui::Node{})) {
        closePreferences();
        return true;
    }
    if (const auto kPage = std::ranges::find(held_.preferencePages, node, &std::pair<ui::Node, std::size_t>::first);
        kPage != held_.preferencePages.end()) {
        held_.preferencePage = kPage->second;
        closePreferences();
        openPreferences();
        return true;
    }
    if (editorField_.has_value() && node == editorField_->value) {
        beginEdit(*editorField_);
        return true;
    }
    if (const auto kShortcut = std::ranges::find(shortcutFields_, node, &FieldRow::value);
        kShortcut != shortcutFields_.end()) {
        beginEdit(*kShortcut);
        return true;
    }
    return false;
}

void ShellParticipant::setKeys(const std::string& command, const std::string& text) {
    const std::optional<std::vector<std::string>> kChords = chordsOf(text);
    if (!kChords.has_value()) {
        ++refused_;
        say("keys refused: \"" + text + "\"; keys are " + std::string{kChordsAre});
    } else {
        const auto kOwn = defaultKeys_.find(command);
        if (kOwn != defaultKeys_.end() && kOwn->second == *kChords) {
            preferences_.keys.erase(command);
        } else {
            preferences_.keys[command] = *kChords;
        }
        ++rebound_;
        keepPreferences();
        // A chord another command has already is said with it.
        std::string shared;
        for (const Command& kOther : commands_) {
            for (const std::string& kChord : *kChords) {
                if (kOther.name != command && std::ranges::find(kOther.keys, kChord) != kOther.keys.end()) {
                    shared += ", " + kChord + " also " + kOther.name + "'s";
                }
            }
        }
        say((kChords->empty() ? command + " has no keys" : command + " is " + chordsText(*kChords)) + shared);
    }
    // Each command's keys as they now are, and the dialog showing them; the
    // rest of Studio as it was.
    for (Command& each : commands_) {
        const auto kGiven = preferences_.keys.find(each.name);
        const auto kOwnKeys = defaultKeys_.find(each.name);
        each.keys = kGiven != preferences_.keys.end() ? kGiven->second
                    : kOwnKeys != defaultKeys_.end()  ? kOwnKeys->second
                                                      : each.keys;
    }
    closePreferences();
    openPreferences();
}

void ShellParticipant::setEditor(const std::string& text) {
    preferences_.editor = text == configuredEditor_ ? std::string{} : text;
    editor_ = text.empty() ? configuredEditor_ : text;
    keepPreferences();
    say("files open with " + editor_);
    closePreferences();
    openPreferences();
}

} // namespace rawframe::studio
