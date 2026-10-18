#include "shell.h"

#include <algorithm>

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
    auto dialog =
        box(root_,
            ui::Layout{.width = ui::pixels(560),
                       .height = ui::share(0.8F),
                       .direction = ui::Direction::Column,
                       .gap = 4,
                       .padding = {12, 12, 12, 12},
                       .placement = {.absolute = true, .x = ui::share(0.5F), .y = ui::share(0.1F), .anchorX = 0.5F},
                       .scroll = ui::Scroll::Vertical},
            kHeaderFill);
    if (!dialog.has_value()) {
        return;
    }
    dialog_ = *dialog;
    static_cast<void>(tree_->setRole(*dialog, ui::Role::Group));
    // Not "Preferences", the command's name, which a screen reader finds
    // in the Window menu (D777).
    static_cast<void>(tree_->setName(*dialog, "Studio preferences"));
    shortcutFields_.clear();
    editorField_.reset();
    // Close first, where a long list never scrolls it away.
    auto close = button(*dialog, "Close");
    if (close.has_value()) {
        dialogClose_ = *close;
    }
    static_cast<void>(row(*dialog, "Editor", kQuiet, kHeaderFill));
    if (auto line = fieldRow("editor", editor_, *dialog, !preferences_.editor.empty()); line.has_value()) {
        editorField_ = FieldRow{.value = line->second, .role = FieldRow::Role::Editor, .field = "editor"};
    }
    // Each command's keys, brighter where the author gave them; a chord
    // two commands share is said under each, the first in the list taking
    // it (D782).
    static_cast<void>(row(*dialog, "Shortcuts", kQuiet, kHeaderFill));
    for (const Command& kCommand : commands_) {
        auto line = fieldRow(
            kCommand.name + " keys", chordsText(kCommand.keys), *dialog, preferences_.keys.contains(kCommand.name));
        if (!line.has_value()) {
            continue;
        }
        shortcutFields_.push_back(
            FieldRow{.value = line->second, .role = FieldRow::Role::Shortcut, .field = kCommand.name});
        for (const std::string& kChord : kCommand.keys) {
            for (const Command& kOther : commands_) {
                if (&kOther != &kCommand && std::ranges::find(kOther.keys, kChord) != kOther.keys.end()) {
                    static_cast<void>(row(*dialog, kChord + " is also " + kOther.name + "'s", kWarning, kHeaderFill));
                }
            }
        }
    }
    // The game's extensions (SPEC-0051's Preferences: Extensions, D874):
    // each by its path, what its doors are of, and its commands, or why it
    // offers none.
    static_cast<void>(row(*dialog, "Extensions", kQuiet, kHeaderFill));
    if (extensions_.empty()) {
        static_cast<void>(
            row(*dialog, "none: an extension is a Kest program under the game's extensions/", kQuiet, kHeaderFill));
    }
    for (const ListedExtension& kExtension : extensions_) {
        std::string doors;
        for (const std::string& kCapability : kExtension.capabilities) {
            doors += (doors.empty() ? "" : ", ") + kCapability;
        }
        static_cast<void>(
            row(*dialog, kExtension.path + "  doors: " + (doors.empty() ? "none" : doors), kText, kHeaderFill));
        if (!kExtension.refused.empty()) {
            static_cast<void>(row(*dialog, kExtension.refused, kWarning, kHeaderFill));
        }
        for (const std::string& kCommand : kExtension.commands) {
            static_cast<void>(row(*dialog, "  " + kCommand, kQuiet, kHeaderFill));
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
    if (node == dialogClose_) {
        closePreferences();
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
