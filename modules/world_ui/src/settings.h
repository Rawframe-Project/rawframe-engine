#pragma once

// What the UI participant reads of a game (D376, D619): its node components
// and which is inside which, the typed words each shows (D427), what its UI
// document starts each presentation component as, its fonts (D386), and its
// style classes (D431), every layout checked against the engine's.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/document.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_ui/world_ui.h"

#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::world_ui {

/// What a node component starts as, as the UI document says (D619).
struct Start {
    schema::ComponentTypeId component;
    ui::NodeFields fields;
};

/// What a game's UI document says (D619): each node component's parent, the
/// typed words each shows (as long as the nodes, or empty for none), and
/// what each presentation component starts as.
struct UiDocument {
    std::vector<std::optional<std::size_t>> parents;
    std::vector<std::optional<schema::ComponentTypeId>> shows;
    std::vector<Start> starts;
};

/// What `text` says as `game`'s UI document, refused as the game's node
/// components and `program`'s layouts do not take it.
[[nodiscard]] result::Result<UiDocument>
documentOf(const world_kest::GameDescription& game, const kest::Program& program, std::string_view text);

/// The game's node components and their parents, and in `starts` what its
/// UI document starts them as; none for a game without any.
[[nodiscard]] result::Result<std::optional<UiSettings>>
settingsOf(const world_kest::GameFiles& files, const kest::Program& program, std::vector<Start>& starts);

} // namespace rawframe::world_ui
