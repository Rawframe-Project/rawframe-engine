#pragma once

// What Studio keeps of how its author arranged it (D779, SPEC-0051 Part 3):
// the docks' sizes, and the games it opened last (D781), in a document of
// Studio's own, never the game's. A file that does not read leaves Studio
// as it starts.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

struct Preferences {
    float hierarchyWidth = 360;
    float inspectorWidth = 360;
    /// The bottom dock's height; nought for its share of the body.
    float dockHeight = 0;
    /// The game descriptions opened last, the latest first, at most
    /// kRecentGames.
    std::vector<std::string> recent;

    friend bool operator==(const Preferences&, const Preferences&) = default;
};

inline constexpr std::size_t kRecentGames = 8;

/// `preferences` with `description` the latest game opened.
[[nodiscard]] Preferences opened(Preferences preferences, const std::string& description);

/// The sizes kept, each held inside what the window can show.
[[nodiscard]] Preferences held(Preferences preferences);

/// The document's preferences, or none when it is not one: its kind,
/// `rawframe.studio.preferences`, its `formatVersion` 1, finite numbers
/// for the sizes it gives, and strings for the games, of which the first
/// kRecentGames are kept.
[[nodiscard]] std::optional<Preferences> preferencesOf(std::string_view document);

/// The document that keeps `preferences`, in one canonical form.
[[nodiscard]] std::string writtenPreferences(const Preferences& preferences);

} // namespace rawframe::studio
