#pragma once

// What Studio keeps of how its author arranged it (D779, SPEC-0051 Part 3):
// the docks' sizes, the games it opened last (D781), and each command's
// keys and the editor it opens a file in (D782), in a document of Studio's
// own, never the game's, which an author may write by hand as well. A file
// that does not read leaves Studio as it starts.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

/// An arrangement of Studio's docks (SPEC-0051 Part 3, D911): the left
/// dock's and the Inspector's widths, the bottom dock's height (nought for
/// its share), and the bottom dock's tab shown, by its name. Never a mode
/// or a second editor.
struct Layout {
    std::string name;
    float hierarchyWidth = 360;
    float inspectorWidth = 360;
    float dockHeight = 0;
    std::string dock = "Assets";

    friend bool operator==(const Layout&, const Layout&) = default;
};

/// The interface scales an author may choose in place of the screen's, in
/// percent (D966), as an editor's own scaling does over the desktop's.
inline constexpr std::array<int, 7> kInterfaceScales = {100, 125, 150, 175, 200, 250, 300};

/// The most panels docked on the right (D980): every bottom one but one.
inline constexpr std::size_t kMostRightPanels = 12;

/// The most layouts an author saves beside the six Studio ships.
inline constexpr std::size_t kMostSavedLayouts = 16;

struct Preferences {
    float hierarchyWidth = 360;
    float inspectorWidth = 360;
    /// The bottom dock's height; nought for its share of the body.
    float dockHeight = 0;
    /// The game descriptions opened last, the latest first, at most
    /// kRecentGames.
    std::vector<std::string> recent;
    /// Commands' keys in place of their own, by command name: chords as
    /// `isChord` reads them, none for a command given no keys.
    std::map<std::string, std::vector<std::string>, std::less<>> keys;
    /// The command that opens a file at a line (`studio.editor`'s form);
    /// empty for the setting's or the default.
    std::string editor;
    /// The layouts the author saved (D911), at most kMostSavedLayouts.
    std::vector<Layout> layouts;
    /// How large Studio draws, in percent: one of kInterfaceScales, or
    /// nought to follow the screen's scale (D966).
    int interfaceScale = 0;
    /// The bottom dock's panels the author docked on the right, beside the
    /// Inspector, by their tabs' names (D980), at most kMostRightPanels.
    std::vector<std::string> rightPanels;

    friend bool operator==(const Preferences&, const Preferences&) = default;
};

inline constexpr std::size_t kRecentGames = 8;

/// `preferences` with `description` the latest game opened.
[[nodiscard]] Preferences opened(Preferences preferences, const std::string& description);

/// The sizes kept, each held inside what the window can show, and an
/// interface scale not among kInterfaceScales the screen's.
[[nodiscard]] Preferences held(Preferences preferences);

/// The document's preferences, or none when it is not one: its kind,
/// `rawframe.studio.preferences`, its `formatVersion` 1, finite numbers
/// for the sizes it gives, and strings for the games, of which the first
/// kRecentGames are kept.
[[nodiscard]] std::optional<Preferences> preferencesOf(std::string_view document);

/// Whether `text` is a chord a shortcut can be: `Ctrl+`, `Shift+`, and
/// `Alt+`, each at most once and in that order, then F1 to F12 but F11
/// (which the window keeps), a capital letter or a digit with Ctrl, or
/// Delete alone, as the window names what is pressed (D777).
[[nodiscard]] bool isChord(std::string_view text) noexcept;

/// The chords `text` lists, separated by commas and spaces, or none when
/// one is not a chord; nothing for empty text.
[[nodiscard]] std::optional<std::vector<std::string>> chordsOf(std::string_view text);

/// The chords as `chordsOf` reads them: `Ctrl+Y, Ctrl+Shift+Z`.
[[nodiscard]] std::string chordsText(const std::vector<std::string>& chords);

/// The document that keeps `preferences`, in one canonical form.
[[nodiscard]] std::string writtenPreferences(const Preferences& preferences);

} // namespace rawframe::studio
