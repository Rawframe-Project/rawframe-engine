#pragma once

// References between a game's UI document and its style sheet kept whole
// in a session: a node's `style` names a class the sheet has, and a
// node made is of a component the game declares a node. Both
// are read as they stand on disk, which is as the session last wrote them.
// Only names a change brings are refused: a game whose document already
// names a class its sheet lacks can still be changed elsewhere.

#include "rawframe/authoring/request.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

namespace rawframe::authoring_session {

/// The game's UI document and style sheet, each empty where it has none.
struct GameDocuments {
    std::filesystem::path ui;
    std::filesystem::path styles;
    /// The game's components of `rawframe.ui.Node`, by name.
    std::set<std::string, std::less<>> nodes;
};

/// The classes the UI document's nodes name that the sheet does not have;
/// none where either is missing or does not read.
[[nodiscard]] std::set<std::uint64_t> unresolvedStyles(const GameDocuments& game);

/// Refused (`validation_failed`) where `request`, to the document at
/// `path`, would set a node's style to a class the sheet does not have or
/// make a node of a component the game does not declare a node, and
/// (`conflict`) where it would remove a class a node names.
[[nodiscard]] result::Status
referencesKept(const GameDocuments& game, const std::filesystem::path& path, const authoring::Request& request);

} // namespace rawframe::authoring_session
