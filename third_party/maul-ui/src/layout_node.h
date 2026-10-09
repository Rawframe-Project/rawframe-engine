// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What layout keeps per node, in an array parallel to the node store's
// slots: the authored style, the published rectangle, the solver's
// cache, and the node's working values as a flex item of its parent.

#ifndef MAUL_UI_SRC_LAYOUT_NODE_H
#define MAUL_UI_SRC_LAYOUT_NODE_H

#include "tree.h"

#include "maul-ui/layout.h"

#include <stdbool.h>

// What a node is sized under: a constraint per axis on its border box,
// its parent's content extents, negative when indefinite, against which
// its Scale+Offset values resolve, and the direction it inherits.
typedef struct muiSizingInput
{
    muiMeasureAxis width;
    muiMeasureAxis height;
    float parentWidth;
    float parentHeight;
    // The direction the node inherits: right to left when set.
    bool rtl;
    // Whether an exact height is the node's own content height: a flex
    // item's that was not stretched, a column item's flexed in a column
    // with no definite height, a root's or an absolute node's automatic
    // one. CSS keeps such a height indefinite (Flexbox 9.8): percentages
    // below do not resolve against it, nor do its stretched items count
    // as definite.
    bool contentHeight;
    // Whether the answer is the content's alone, the node's aspect ratio
    // and its own limits left out: a flex item's base from its content
    // is its max-content size, unclamped (Flexbox 9.2.3), and a column's
    // automatic minimum for a ratio item reads its content's width
    // (section 4.5, as Chrome).
    bool contentOnly;
} muiSizingInput;

typedef struct muiCacheEntry
{
    muiSizingInput input;
    muiSize size;
    bool valid;
} muiCacheEntry;

enum
{
    // A power of two that fits the cache's next field.
    MUI_CACHE_ENTRIES = 4
};

// Sizing results by input, valid until the node or a descendant changes,
// and the size the node was last laid out at in full.
typedef struct muiLayoutCache
{
    muiCacheEntry entries[MUI_CACHE_ENTRIES];
    // The entry the next miss replaces, of MUI_CACHE_ENTRIES; whether a
    // node below resolves a size against its parent's extents
    // (muiScaledBelow): 0 not yet known, 1 none does, 2 one does.
    uint8_t next : 2;
    uint8_t scaledBelow : 2;
    // On which axes a content size is the content's own, so answers an
    // exact or limited query (src/solve.c): 0 not yet known, else 4 with
    // 1 for the width and 2 for the height.
    uint8_t loose : 3;
    // Whether a miss replaced a valid entry since the cache was cleared:
    // a query its parent asked may be lost (src/layout_bound.c); and the
    // direction and content height the node was last laid out with.
    bool replaced : 1;
    bool finalValid : 1;
    bool finalRtl : 1;
    bool finalContentHeight : 1;
    muiSize finalSize;
} muiLayoutCache;

// A node's values while its parent runs the flex algorithm over it, all
// along the parent's main axis unless named cross. Sizes are border box.
typedef struct muiFlexItemState
{
    float marginMain;
    float marginCross;
    float minMain;
    float maxMain;
    float minCross;
    float maxCross;
    float base;
    union
    {
        // The base without padding and border, which scales shrinking,
        // while main sizes resolve.
        float innerBase;
        // Then, for a baseline-aligned item: its baseline's distance from
        // its outer cross start, its margin included.
        float ascent;
    };
    float hypothetical;
    float target;
    float cross;
    // The last clamp's effect: 1 raised to the minimum, -1 lowered to the
    // maximum, 0 neither.
    int8_t violation;
    bool frozen;
    // The automatic minimum is not computed yet: it only matters when the
    // line shrinks, as a content-based base is never below it.
    bool minimumPending;
    // Set on the first child of each line: whether any of its children
    // align by baseline, how many it holds, its cross size and its offset
    // from the container's cross start, in the direction lines follow.
    bool lineBaselines;
    uint32_t lineCount;
    float lineCross;
    float lineOffset;
} muiFlexItemState;

typedef struct muiLayoutNode
{
    muiLayoutStyle style;
    muiRect rect;
    muiLayoutCache cache;
    muiFlexItemState item;
    // style.placement.position == mui_positionAbsolute, or popped, kept
    // beside item because every walk over a container's flex items reads
    // it.
    bool absolute;
    // The direction the node was last laid out in: right to left when set.
    bool rtl;
    // What the node's conditions read when it was last styled (bits of
    // muiConditionReads) and the size and direction they read, so that a
    // layout that changes them styles the node again.
    uint8_t conditionReads;
    bool conditionRtl;
    muiSize conditionSize;
    // Set while the node's conditions are held after an oscillation
    // between the two sizes here: layouts that give one of them do not
    // style it again, and any other size releases it.
    bool held;
    // Set while the node exits popped (mui_exitPop): out of the flow at
    // the rectangle it had. Here, in padding before heldSizes.
    bool popped;
    // Set while the node is bound to an item of a virtual list: out of
    // the flow, stretched across the list, placed along it by
    // src/virtual.c. In padding too.
    bool listed;
    muiSize heldSizes[2];
} muiLayoutNode;

// Nodes are whole cache lines, so that every node's rectangle and cache
// sit on the same lines in each.
static_assert(sizeof(muiLayoutNode) % 64 == 0, "a layout node is whole cache lines");

// Whether a dimension scales its parent's extent.
static inline bool muiIsScaled(muiDimension dimension)
{
    return dimension.kind == mui_dimensionValue && dimension.scale != 0.0f;
}

// Whether a node below node sizes itself from a definite size above it:
// a size resolved against its parent's extents, or an aspect ratio,
// which a stretched cross size makes definite. A content query leaves
// those sizes indefinite where an exact or limited one may give them, so
// a content size then answers content queries alone. Kept in the cache
// and cleared with it, as any change below clears it.
static inline bool muiScaledBelow(const muiTree* tree, muiLayoutNode* nodes, uint32_t node)
{
    muiLayoutCache* cache = &nodes[node - 1].cache;
    if (cache->scaledBelow == 0)
    {
        bool scaled = false;
        for (uint32_t c = muiTreeAt(tree, node)->links.firstChild; c != 0 && !scaled;
             c = muiTreeAt(tree, c)->links.next)
        {
            const muiLayoutStyle* style = &nodes[c - 1].style;
            const muiSizing* sizing = &style->sizing;
            scaled = muiIsScaled(sizing->width) || muiIsScaled(sizing->height) ||
                     muiIsScaled(sizing->minWidth) || muiIsScaled(sizing->maxWidth) ||
                     muiIsScaled(sizing->minHeight) || muiIsScaled(sizing->maxHeight) ||
                     muiIsScaled(style->item.basis) || sizing->aspectRatio != 0.0f ||
                     muiScaledBelow(tree, nodes, c);
        }
        cache->scaledBelow = scaled ? 2 : 1;
    }
    return cache->scaledBelow == 2;
}

// A node's content box in its border box, as layout sized it: inside the
// border and the padding it was laid out with, whose start is the right in
// a right-to-left node.
static inline muiRect muiContentBoxOf(const muiLayoutNode* layout, const muiEdges* padding)
{
    const muiEdges* border = &layout->style.border;
    float start = border->start + padding->start;
    float end = border->end + padding->end;
    float top = border->top + padding->top;
    float bottom = border->bottom + padding->bottom;
    float width = layout->rect.width - start - end;
    float height = layout->rect.height - top - bottom;
    return (muiRect){layout->rtl ? end : start, top, width > 0.0f ? width : 0.0f,
                     height > 0.0f ? height : 0.0f};
}

static inline bool muiIsSameRect(muiRect a, muiRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Brings the values kept beside a node's style up to date after the
// style changes.
static inline void muiSyncLayoutNode(muiLayoutNode* node)
{
    node->absolute =
        node->style.placement.position == mui_positionAbsolute || node->popped || node->listed;
}

// The first child of a container that takes part in its flex layout, and
// the next after child: absolute children are skipped. 0 at the end.
static inline uint32_t muiFirstFlowAt(const muiTree* tree, const muiLayoutNode* nodes,
                                      uint32_t node)
{
    uint32_t at = node;
    while (at != 0 && nodes[at - 1].absolute)
    {
        at = muiTreeAt(tree, at)->links.next;
    }
    return at;
}

static inline uint32_t muiFirstFlowChild(const muiTree* tree, const muiLayoutNode* nodes,
                                         uint32_t container)
{
    return muiFirstFlowAt(tree, nodes, muiTreeAt(tree, container)->links.firstChild);
}

static inline uint32_t muiNextFlowChild(const muiTree* tree, const muiLayoutNode* nodes,
                                        uint32_t child)
{
    return muiFirstFlowAt(tree, nodes, muiTreeAt(tree, child)->links.next);
}

#endif // MAUL_UI_SRC_LAYOUT_NODE_H
