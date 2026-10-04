// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Region contours: the boundary of each region traced along its
// cells and simplified, each vertex remembering what lies across the edge
// that starts at it.

#ifndef MAUL_NAV_SRC_CONTOUR_H
#define MAUL_NAV_SRC_CONTOUR_H

#include "allocator.h"
#include "compact.h"
#include "region.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// The vertex is a corner on the tile's border between two interior
// cells, removed when polygons are built so that tiles meet.
#define MNAV_VERTEX_TILE_BORDER 1u
// The edge from this vertex to the next separates two areas.
#define MNAV_EDGE_AREA_BORDER 2u

// A contour vertex, in cells of the tile without its border (x, z) and
// cell heights (y), and the region across the edge from it to the next
// vertex: 0 for a wall, a region, or a border region.
typedef struct mnavContourVertex
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t neighbor;
    uint8_t flags;
} mnavContourVertex;

// The vertices of a contour are vertices[first] to vertices[first +
// count - 1] of its set. An outline winds one way and a hole the other.
typedef struct mnavContour
{
    int32_t first;
    int32_t count;
    uint32_t region;
    mnavAreaType area;
    bool hole;
} mnavContour;

typedef struct mnavContourSet
{
    mnavContour* contours;
    int32_t count;
    int32_t capacity;
    mnavContourVertex* vertices;
    int32_t vertexCount;
    int32_t vertexCapacity;
    // Holes dropped by mnavMergeHoles: without a bridge to their outline,
    // or without an outline.
    int32_t droppedHoles;
} mnavContourSet;

// Traces and simplifies the contours of every region of a field with a
// border of border cells, to within edgeError cells along walls and area
// borders, splitting walls longer than edgeLength cells (0 for no limit).
mnavResult mnavBuildContours(mnavMemory* memory, const mnavCompactField* field,
                             const mnavRegionMap* regions, int32_t border, float edgeError,
                             int32_t edgeLength, mnavContourSet* set);

void mnavReleaseContours(mnavMemory* memory, mnavContourSet* set);

// True when, going round a corner through the cells keyed (area << 32 |
// region, 0 for none), two cells of one border region are followed by two
// interior cells of one area: a vertex on the tile's border that polygons
// later remove so that neighboring tiles meet.
bool mnavIsTileBorderCorner(const uint64_t keys[4]);

#endif // MAUL_NAV_SRC_CONTOUR_H
