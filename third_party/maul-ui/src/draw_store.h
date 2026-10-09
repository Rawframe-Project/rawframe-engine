// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's draw lists: two of each table, the last list and the one
// being built from it, reserved by the context's limits, and what each
// node was last painted as, so that a clean subtree copies its commands
// (record mui-0005).

#ifndef MAUL_UI_SRC_DRAW_STORE_H
#define MAUL_UI_SRC_DRAW_STORE_H

#include "maul-ui/draw.h"

#include <stdbool.h>
#include <stdint.h>

// A span of a table: from first up to, not including, end.
typedef struct muiDrawRange
{
    uint32_t first;
    uint32_t end;
} muiDrawRange;

// A node as the build numbered build painted it: its border box
// relative to its parent, its origin on the surface, the opacity it was
// painted in, and the spans of the tables its subtree filled; and,
// during the build that paints it, the clip and opacity it passes to its
// children; the transform its commands go through and the one its
// children's do, a scroll container's own (record mui-0007). A copied
// node's clip and opacity are not set, as its children are not visited.
typedef struct muiPaintState
{
    uint64_t build;
    muiRect rect;
    float x;
    float y;
    uint32_t clip;
    float opacity;
    float inherited;
    uint32_t transform;
    uint32_t inner;
    muiDrawRange commands;
    muiDrawRange clips;
    muiDrawRange gradients;
    muiDrawRange transforms;
    // How many nodes of its subtree, outside layers, had host content
    // that asked what is visible, which a copy would not keep right; and
    // during its build, how many had before it.
    uint32_t culled;
    uint32_t culledBefore;
} muiPaintState;

// One list's tables. Entry 0 of the clips and gradients is the
// placeholder for none; glyphs have none, as runs name spans.
// Marks a transform's owner as one of a local scale.
#define MUI_TRANSFORM_SCALE 0x80000000u

typedef struct muiDrawTables
{
    muiDrawCommand* commands;
    muiDrawClip* clips;
    muiDrawGradient* gradients;
    muiGlyph* glyphs;
    // Entry 0 is the identity; each other is owned by a node (its slot):
    // a scroll container's, a translation by its offset, or with
    // MUI_TRANSFORM_SCALE a scaled node's, a scale about its origin, after
    // its parent entry's.
    muiDrawTransform* transforms;
    uint32_t* transformOwners;
    uint32_t* transformParents;
    uint32_t commandCount;
    uint32_t clipCount;
    uint32_t gradientCount;
    uint32_t glyphCount;
    uint32_t transformCount;
} muiDrawTables;

typedef struct muiDrawStore
{
    muiDrawHeader header;
    // The list shown, and the other, which the next build writes.
    muiDrawTables tables[2];
    uint32_t current;
    // Capacities; those of clips and gradients count the placeholder.
    uint32_t commandCapacity;
    uint32_t clipCapacity;
    uint32_t gradientCapacity;
    uint32_t glyphCapacity;
    uint32_t transformCapacity;
    // How many nodes' host content asked what is visible in the shown
    // list: scrolling alone then builds a list of its own.
    uint32_t culled;
    // The root slot of the shown list. Its header's scale is 0 when there
    // is no list to take from: none was built yet, or the last build
    // failed.
    uint32_t rootIndex;
    // Per node, parallel to the tree's slots.
    muiPaintState* states;
} muiDrawStore;

#endif // MAUL_UI_SRC_DRAW_STORE_H
