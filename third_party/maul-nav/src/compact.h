// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The open-space field: the walkable spans of a heightfield as the
// open space above them, each linked to the spans an agent can step to
// in the four directions.

#ifndef MAUL_NAV_SRC_COMPACT_H
#define MAUL_NAV_SRC_COMPACT_H

#include "allocator.h"
#include "heightfield.h"
#include "raster.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// A link to no span.
#define MNAV_NO_LINK 0xFFFFu

// The directions, in order: -X, +Z, +X, -Z.
static inline int32_t mnavDirectionX(int32_t direction)
{
    return direction == 0 ? -1 : (direction == 2 ? 1 : 0);
}

static inline int32_t mnavDirectionZ(int32_t direction)
{
    return direction == 1 ? 1 : (direction == 3 ? -1 : 0);
}

// The open space above a walkable span: from floor up height cells, and
// for each direction the layer index of the neighbor span it links to
// within that neighbor's column, or MNAV_NO_LINK.
typedef struct mnavOpenSpan
{
    uint16_t floor;
    uint16_t height;
    uint16_t links[4];
} mnavOpenSpan;

// The open spans of column (x, z) are spans[columns[i]] up to, not
// including, spans[columns[i + 1]] with i = x + z * width, lowest first;
// areas holds one area per span.
typedef struct mnavCompactField
{
    mnavTileFrame frame;
    uint32_t* columns;
    mnavOpenSpan* spans;
    mnavAreaType* areas;
    int32_t spanCount;
} mnavCompactField;

// Builds the open-space field of a filtered heightfield's walkable spans
// for an agent of height and step cells.
mnavResult mnavBuildCompactField(mnavMemory* memory, const mnavHeightfield* heightfield,
                                 int32_t height, int32_t step, mnavCompactField* field);

void mnavReleaseCompactField(mnavMemory* memory, mnavCompactField* field);

// Allocates a field of spanCount spans over a frame, its contents unset;
// on failure it holds nothing.
mnavResult mnavAllocateCompactField(mnavMemory* memory, const mnavTileFrame* frame,
                                    int32_t spanCount, mnavCompactField* field);

// Links each span to the span it reaches in each neighboring column: one
// sharing at least height cells of space, its floor within step.
void mnavLinkCompactField(mnavCompactField* field, int32_t height, int32_t step);

// The index of the span linked from span i of column (x, z) in a
// direction; the link must exist.
static inline uint32_t mnavLinkedSpan(const mnavCompactField* field, int32_t x, int32_t z,
                                      uint32_t i, int32_t direction)
{
    int32_t column =
        x + mnavDirectionX(direction) + (z + mnavDirectionZ(direction)) * field->frame.width;
    return field->columns[column] + field->spans[i].links[direction];
}

#endif // MAUL_NAV_SRC_COMPACT_H
