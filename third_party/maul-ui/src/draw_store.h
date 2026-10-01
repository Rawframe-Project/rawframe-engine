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
// children. A copied node's are not set, as its children are not visited.
typedef struct muiPaintState
{
    uint64_t build;
    muiRect rect;
    float x;
    float y;
    uint32_t clip;
    float opacity;
    float inherited;
    muiDrawRange commands;
    muiDrawRange clips;
    muiDrawRange gradients;
} muiPaintState;

// One list's tables. Entry 0 of the clips and gradients is the
// placeholder for none; glyphs have none, as runs name spans.
typedef struct muiDrawTables
{
    muiDrawCommand* commands;
    muiDrawClip* clips;
    muiDrawGradient* gradients;
    muiGlyph* glyphs;
    uint32_t commandCount;
    uint32_t clipCount;
    uint32_t gradientCount;
    uint32_t glyphCount;
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
    // The root slot of the shown list. Its header's scale is 0 when there
    // is no list to take from: none was built yet, or the last build
    // failed.
    uint32_t rootIndex;
    muiDrawTransform identity;
    // Per node, parallel to the tree's slots.
    muiPaintState* states;
} muiDrawStore;

#endif // MAUL_UI_SRC_DRAW_STORE_H
