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
} muiSizingInput;

typedef struct muiCacheEntry
{
    muiSizingInput input;
    muiSize size;
    bool valid;
} muiCacheEntry;

enum
{
    MUI_CACHE_ENTRIES = 4
};

// Sizing results by input, valid until the node or a descendant changes,
// and the size the node was last laid out at in full.
typedef struct muiLayoutCache
{
    muiCacheEntry entries[MUI_CACHE_ENTRIES];
    // The entry the next miss replaces.
    uint8_t next;
    bool finalValid;
    bool finalRtl;
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
    // The base without padding and border, which scales shrinking.
    float innerBase;
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
    // Set on the first child of each line: how many children the line
    // holds, its cross size and its offset from the container's cross
    // start, in the direction lines follow.
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
    // style.placement.position == mui_positionAbsolute, kept beside item
    // because every walk over a container's flex items reads it.
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
    muiSize heldSizes[2];
} muiLayoutNode;

static inline bool muiIsSameRect(muiRect a, muiRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Brings the values kept beside a node's style up to date after the
// style changes.
static inline void muiSyncLayoutNode(muiLayoutNode* node)
{
    node->absolute = node->style.placement.position == mui_positionAbsolute;
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
