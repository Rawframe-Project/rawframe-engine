#pragma once

// A game's UI document (SPEC-0030, D619): its UI nodes as a tree, each a
// node component the game declares, with what it starts as and which typed
// words it shows. A `ui.document` under SPEC-0028's canonical JSON profile,
// as the game's styles are (D431): SPEC-0030's Luau-literal document is
// suspended under ADR-0084, and this is its generation one for Kest.
//
//   {
//     "kind": "ui.document",
//     "formatVersion": 1,
//     "nodes": [
//       {
//         "component": "game.hud",
//         "widthOffset": 240,
//         "alignItems": "center",
//         "fill": "#101820c0",
//         "children": [
//           {
//             "component": "game.points",
//             "text": "f1182d0420cc09a0",
//             "textSize": 14
//           }
//         ]
//       }
//     ]
//   }
//
// A node names its `component`, once in a document; SPEC-0030's `ref` is
// that name, the address the game's present systems write the node by.
// `shows` names a `rawframe.ui.Typed` component whose words it shows in
// place of its label (D427). Then any of `rawframe.ui.Node`'s fields, in
// its order, at their defaults omitted: numbers as they are; colors
// `#rrggbbaa`; identities (`image`, `text`, `font`, `style`) 16 lowercase
// hexadecimal digits; `absolute`, `clip`, and `thumb` as `true`; a closed set by the
// lowercase name of its case (`direction`: row, row_reverse, column,
// column_reverse; `justify`: start, end, center, space_between,
// space_around, space_evenly; `alignItems`, `alignSelf`: auto, stretch,
// start, end, center; `gradientKind`: none, linear, radial; `textAlign`:
// start, center, end; `textWrap`: wrap, no_wrap; `hit`: children, self,
// none; `layer`: none, activation, modal, overlay; `edit`: none, line,
// message, lines; `scroll` and `range`: none, horizontal, vertical, and
// for `scroll` both; a range's numbers within a million of nought). Last,
// `children`, the nodes inside it. Siblings are drawn by `order`, as
// without a document.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::ui {

/// `rawframe.ui.Node` as Kest lays it out (rawframe/ui.kest): what a node
/// component holds, and what a UI document starts one as.
struct NodeFields {
    float widthScale = 0;
    float widthOffset = 0;
    float heightScale = 0;
    float heightOffset = 0;
    std::uint32_t direction = 0;
    std::uint32_t justify = 0;
    std::uint32_t alignItems = 0;
    std::uint32_t alignSelf = 0;
    float gap = 0;
    float grow = 0;
    float shrink = 0;
    float padding = 0;
    float margin = 0;
    float border = 0;
    std::uint32_t absolute = 0;
    float xScale = 0;
    float xOffset = 0;
    float yScale = 0;
    float yOffset = 0;
    float anchorX = 0;
    float anchorY = 0;
    std::uint32_t fill = 0;
    std::uint32_t borderColor = 0;
    float radius = 0;
    std::uint32_t clip = 0;
    std::int32_t order = 0;
    std::uint64_t image = 0;
    std::uint32_t imageTint = 0;
    float imageSlice = 0;
    std::uint32_t shadowColor = 0;
    float shadowX = 0;
    float shadowY = 0;
    float shadowBlur = 0;
    std::uint32_t gradientKind = 0;
    float gradientAngle = 0;
    std::uint32_t gradientFrom = 0;
    std::uint32_t gradientTo = 0;
    std::uint64_t text = 0;
    std::int64_t textValue = 0;
    std::uint64_t font = 0;
    float textSize = 0;
    std::uint32_t textColor = 0;
    std::uint32_t textAlign = 0;
    std::uint32_t textWrap = 0;
    std::int64_t press = 0;
    std::uint32_t hit = 0;
    std::uint32_t layer = 0;
    std::uint32_t edit = 0;
    std::uint32_t editLimit = 0;
    std::uint64_t style = 0;
    std::uint32_t scroll = 0;
    std::uint32_t range = 0;
    float rangeMinimum = 0;
    float rangeMaximum = 0;
    float rangeStep = 0;
    float rangeValue = 0;
    std::uint32_t thumb = 0;
    /// The four bytes Kest's layout pads the node with, named and nought,
    /// so a node compared byte for byte reads no stray padding.
    std::uint32_t reserved = 0;
};

/// One node of a document.
struct DocumentNode {
    std::string component;
    /// The `rawframe.ui.Typed` component it shows; empty for none.
    std::string shows;
    NodeFields fields;
    /// The node it is inside, an index of the document's; none for a root.
    std::optional<std::size_t> parent;
};

/// Every node, each before those inside it, siblings in the order written.
struct UiDocument {
    std::vector<DocumentNode> nodes;
};

/// The named limit points of a UI document; generation one's.
struct DocumentLimits {
    std::size_t maximumNodes = 1024;
    /// Nodes inside nodes, a root the first.
    std::size_t maximumDepth = 24;
    std::size_t maximumNameLength = 128;
};

/// Reads a `ui.document` of format version 1: canonical bytes, the version,
/// each node's fields in order and at no default, components named once,
/// values in their ranges, limits. Refusals are `rawframe.document` errors
/// naming the field by its path.
[[nodiscard]] result::Result<UiDocument> readDocument(std::string_view text, const DocumentLimits& limits = {});

} // namespace rawframe::ui
