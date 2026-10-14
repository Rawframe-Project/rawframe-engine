#pragma once

// How a node's fields (D619) lay out and look in a tree: what a game's node
// is given as its components change (`rawframe.world_ui`, D376), and what a
// tool shows of a UI document it previews.

#include "rawframe/ui/document.h"
#include "rawframe/ui/tree.h"

#include <optional>

namespace rawframe::ui {

/// `node`'s layout as the tree takes it; none for a number past its
/// constants.
[[nodiscard]] std::optional<Layout> layoutOf(const NodeFields& node) noexcept;
/// `node`'s look; a gradient kind past the set is passed on, for the tree
/// to refuse.
[[nodiscard]] Look lookOf(const NodeFields& node) noexcept;
/// The parts of its look a styled node sets itself (D431): those it gives
/// a value other than nought. An inner shadow it has no field for.
[[nodiscard]] LookParts ownPartsOf(const NodeFields& node) noexcept;
/// `node`'s words' look in `font`, 16 pixels where it gives no size.
[[nodiscard]] TextLook textLookOf(const NodeFields& node, Font font) noexcept;

} // namespace rawframe::ui
