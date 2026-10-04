// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's polygon mesh: the rings of its regions triangulated,
// welded into one vertex list, merged into convex polygons of at most
// MNAV_POLYGON_VERTICES vertices and linked to their neighbors.

#ifndef MAUL_NAV_SRC_POLYMESH_H
#define MAUL_NAV_SRC_POLYMESH_H

#include "allocator.h"
#include "contour.h"

#include "maul-nav/bake.h"

#include <stdint.h>

#define MNAV_POLYGON_VERTICES 6
// No vertex or no polygon.
#define MNAV_NO_INDEX 0xFFFFu
// The region of a polygon built from more than one region; contours
// never carry it.
#define MNAV_MIXED_REGION 0u

// A convex polygon, counter-clockwise seen from above like the rings.
// neighbors[k] is the polygon across the edge from vertices[k] to the next
// vertex, or MNAV_NO_INDEX; sides[k] is 1 to 4 when that edge lies on
// the tile's -X, +Z, +X or -Z side with nothing across it yet, else 0.
typedef struct mnavPolygon
{
    uint16_t vertices[MNAV_POLYGON_VERTICES];
    uint16_t neighbors[MNAV_POLYGON_VERTICES];
    uint8_t sides[MNAV_POLYGON_VERTICES];
    uint8_t count;
    mnavAreaType area;
    uint32_t region;
} mnavPolygon;

// A mesh vertex, in cells of the tile without its border (x, z) and
// offset cell heights (y).
typedef struct mnavMeshVertex
{
    uint16_t x;
    uint16_t y;
    uint16_t z;
} mnavMeshVertex;

// removable marks the tile-border vertices (MNAV_VERTEX_TILE_BORDER) a
// later step removes; it has vertexCapacity entries like vertices.
typedef struct mnavPolyMesh
{
    mnavMeshVertex* vertices;
    uint8_t* removable;
    int32_t vertexCount;
    int32_t vertexCapacity;
    mnavPolygon* polygons;
    int32_t polygonCount;
    int32_t polygonCapacity;
    int32_t tileCells;
    // Rings whose triangulation ran out of ears and kept only part.
    int32_t failedRings;
} mnavPolyMesh;

// Builds the polygons of a tile of tileCells cells from its merged
// contours, not yet linked. Returns mnav_errorLimit past maxVertices
// vertices or maxPolygons polygons.
mnavResult mnavBuildPolyMesh(mnavMemory* memory, const mnavContourSet* set, int32_t tileCells,
                             int32_t maxVertices, int32_t maxPolygons, mnavPolyMesh* mesh);

// Merges polygons of one area pairwise into convex polygons of at most
// MNAV_POLYGON_VERTICES vertices, the pair with the longest shared edge
// first, the first pair in order on ties, until no pair can merge.
// Returns the number left at the front of polygons.
int32_t mnavMergePolygons(const mnavMeshVertex* vertices, mnavPolygon* polygons, int32_t count);

// Fills each polygon's neighbors and sides, the last step of the mesh.
mnavResult mnavLinkPolyMesh(mnavMemory* memory, mnavPolyMesh* mesh);

void mnavReleasePolyMesh(mnavMemory* memory, mnavPolyMesh* mesh);

#endif // MAUL_NAV_SRC_POLYMESH_H
