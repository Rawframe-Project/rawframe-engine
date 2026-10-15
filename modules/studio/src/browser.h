#pragma once

// Studio's asset browser (D799): every source the game's directory holds,
// as the session lists it (`authoring.assets`' sources), narrowed to a
// folder and a kind, each with how the session's cooks left it. Scenes and
// the documents Studio edits open from it as before; another source is
// chosen, and said with its kind and cook state.

#include "rawframe/ui/tree.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

/// A source as the session lists it: its path from the game's directory,
/// its folder, its importer, and its cook state (`cooked`, `failed`,
/// `cooking`, `unknown`) with the cook's words when it failed.
struct ListedSource {
    std::string path;
    std::string folder;
    std::string importer;
    std::string cook;
    std::string problem;
};

/// The sources an `authoring.assets` answer lists; none for a refusal, and
/// an entry out of shape left out.
[[nodiscard]] std::vector<ListedSource> sourcesOf(std::string_view reply);

/// The kind an importer makes, as the browser filters by it: its name after
/// `rawframe.`.
[[nodiscard]] std::string kindOf(std::string_view importer);

/// The browser's filter control for `kind`: "Textures" for `texture`, the
/// kind itself for one it does not name.
[[nodiscard]] std::string kindLabel(std::string_view kind);

struct AssetBrowser {
    std::vector<ListedSource> sources;
    /// The folder shown, none for every folder; the kind shown, none for
    /// every kind.
    std::optional<std::string> folder;
    std::optional<std::string> kind;
    /// The source chosen that is no document, if any.
    std::string chosen;
    /// The rows: a control and what it shows (a kind, a folder, a source).
    std::vector<std::pair<ui::Node, std::string>> kindRows;
    std::vector<std::pair<ui::Node, std::string>> folderRows;
    std::vector<std::pair<ui::Node, std::string>> sourceRows;
    std::vector<ui::Node> boxes;
    /// How many times a source was chosen, and the folder and kind narrowed.
    std::uint64_t chosenCount = 0;
    std::uint64_t narrowed = 0;

    /// The source listed at `path`, if any.
    [[nodiscard]] const ListedSource* find(std::string_view path) const;
    /// Whether `path` is shown under the folder and kind chosen.
    [[nodiscard]] bool shows(std::string_view path) const;
};

} // namespace rawframe::studio
