#pragma once

// Studio's asset browser (D799): every source the game's directory holds,
// as the session lists it (`authoring.assets`' sources), narrowed to a
// folder and a kind, each with how the session's cooks left it. Scenes and
// the documents Studio edits open from it as before; another source is
// chosen, and said with its kind and cook state.

#include "rawframe/texture/texture.h"
#include "rawframe/ui/tree.h"

#include <cstdint>
#include <map>
#include <memory>
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
    /// Its resource identity, as a document names it (D827).
    std::string id;
};

/// The sources an `authoring.assets` answer lists; none for a refusal, and
/// an entry out of shape left out.
[[nodiscard]] std::vector<ListedSource> sourcesOf(std::string_view reply);

/// The folders an `authoring.assets` answer lists, those with nothing in
/// them too (D929).
[[nodiscard]] std::vector<std::string> foldersOf(std::string_view reply);

/// The kind an importer makes, as the browser filters by it: its name after
/// `rawframe.`.
[[nodiscard]] std::string kindOf(std::string_view importer);

/// The image key a source's thumbnail is shown by (D801): its path's,
/// apart from Studio's other images.
[[nodiscard]] std::uint64_t thumbnailKey(std::string_view path) noexcept;

/// The browser's filter control for `kind`: "Textures" for `texture`, the
/// kind itself for one it does not name.
[[nodiscard]] std::string kindLabel(std::string_view kind);

/// A card in the Assets grid (D916): its name, the kind it is drawn as,
/// what it says it is, its picture's image key (nought for the kind's
/// icon), the dot's color where its cook is behind (nought for none), and
/// whether it failed and is chosen.
struct AssetCard {
    std::string name;
    std::string kind;
    std::string what;
    std::uint64_t picture = 0;
    std::uint32_t dot = 0;
    bool failed = false;
    bool chosen = false;
};

/// Whether the file name at the end of `path` holds `search`, any case;
/// every name holds an empty one (D916).
[[nodiscard]] bool named(std::string_view path, std::string_view search);

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
    /// The game's scripts' cards, each by its path in the game (D919).
    std::vector<std::pair<ui::Node, std::string>> scriptRows;
    std::vector<ui::Node> boxes;
    /// The pane as the design draws it (D916): its heading's parts, the
    /// folders' column, and the cards' grid, kept while the game is open
    /// so the grid keeps its scroll and the search its typing; what is in
    /// them is made again, as `boxes`.
    ui::Node crumbs{};
    ui::Node chips{};
    ui::Node tools{};
    ui::Node folders{};
    ui::Node grid{};
    /// The search's field and what it narrows the names to, any case.
    ui::Node searchValue{};
    std::string search;
    /// The heading's Import, which asks for the files to bring in.
    ui::Node import{};
    /// The Inspector's Reimport for the chosen source, and how many times
    /// its settings were set and refused (D800).
    ui::Node reimport{};
    /// The thumbnails' size, none, small, or large (D801), its controls,
    /// and each texture's picture by the image key its row shows, made from
    /// the last cook, and how many were made.
    std::uint32_t thumbnail = 128;
    std::vector<std::pair<ui::Node, std::uint32_t>> sizeRows;
    std::map<std::uint64_t, std::shared_ptr<const texture::Texture>> pictures;
    std::uint64_t thumbnailsMade = 0;
    /// Whether the session said meshes' thumbnails are being drawn, and
    /// when the browser last asked again (D899).
    bool drawing = false;
    double drawingAsked = 0;
    /// Files dropped on the window, and those imported (D802).
    /// The Inspector's References, the center's rows of what the chosen
    /// source uses and what uses it, and how many times they were shown
    /// (D803).
    ui::Node references{};
    std::vector<std::pair<ui::Node, std::string>> referenceRows;
    std::vector<ui::Node> referenceBoxes;
    std::uint64_t referencesShown = 0;
    std::uint64_t dropped = 0;
    std::uint64_t droppedImported = 0;
    std::uint64_t settingsSet = 0;
    std::uint64_t settingsRefused = 0;
    /// How many times a source was chosen, and the folder and kind narrowed.
    std::uint64_t chosenCount = 0;
    std::uint64_t narrowed = 0;
    /// Every folder the session lists (D929); what New folder, Rename and
    /// Delete act on, the card or folder last pressed, by its path under
    /// the game's directory; the heading's controls for them; the name
    /// being typed, a new folder's or the target's, in its field; the
    /// target asked to be deleted, and its confirmation's controls.
    std::vector<std::string> folderList;
    std::string target;
    ui::Node newFolder{};
    ui::Node rename{};
    ui::Node remove{};
    enum class Typing : std::uint8_t {
        None,
        Folder,
        Rename
    };
    Typing typing = Typing::None;
    ui::Node typed{};
    std::string removing;
    ui::Node removeSure{};
    ui::Node removeKeep{};
    /// A card held, dragged onto a folder or the game's own row to move it
    /// there; that row.
    std::optional<std::string> held;
    ui::Node rootRow{};
    /// A scene's row pressed, opened as it is let go unless it was dragged
    /// to be placed (D975); and how many assets a drag placed.
    std::optional<std::size_t> opening;
    bool choosing = false;
    std::uint64_t placed = 0;
    /// How many files were moved and removed, and folders made.
    std::uint64_t moved = 0;
    std::uint64_t removed = 0;
    std::uint64_t foldersMade = 0;

    /// The source listed at `path`, if any.
    [[nodiscard]] const ListedSource* find(std::string_view path) const;
    /// Whether `path` is shown under the folder and kind chosen.
    [[nodiscard]] bool shows(std::string_view path) const;
};

} // namespace rawframe::studio
