// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's detail mesh: the floor's heights laid over each polygon
// as small triangles, in sixteenths of a cell on the ground and cell
// heights up, with exact integer geometry.

#ifndef MAUL_NAV_SRC_DETAIL_H
#define MAUL_NAV_SRC_DETAIL_H

#include "allocator.h"
#include "compact.h"
#include "delaunay.h"
#include "polymesh.h"
#include "region.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// The most detail vertices one polygon holds, and so at most 252
// triangles.
#define MNAV_DETAIL_VERTICES 127
// The most samples inside one polygon edge.
#define MNAV_DETAIL_EDGE_SAMPLES 20

// A polygon's detail: its vertices (the polygon's own first) and
// triangles.
typedef struct mnavDetailPart
{
    int32_t firstVertex;
    int32_t firstTriangle;
    uint8_t vertexCount;
    uint8_t triangleCount;
} mnavDetailPart;

typedef struct mnavDetailMesh
{
    mnavDetailPart* parts;
    int32_t partCount;
    mnavDetailVertex* vertices;
    int32_t vertexCount;
    int32_t vertexCapacity;
    mnavDetailTriangle* triangles;
    int32_t triangleCount;
    int32_t triangleCapacity;
    // Samples that found no height within the search radius and took the
    // polygon's own.
    int32_t fallbackHeights;
    // Polygons whose outline could not be triangulated to the end.
    int32_t failedPolygons;
    // Polygons that reached MNAV_DETAIL_VERTICES with samples still
    // beyond the maximum error.
    int32_t cappedPolygons;
} mnavDetailMesh;

// What the detail mesh samples, from the bake's cells: the sample
// distance and maximum error in sixteenths (of a cell, of a cell height),
// the radius in cells a height lookup searches, and the field's border.
typedef struct mnavDetailSettings
{
    int32_t sample;
    int32_t error;
    int32_t radius;
    int32_t border;
} mnavDetailSettings;

// Builds the detail mesh of a linked polygon mesh from the open-space
// field and its regions.
mnavResult mnavBuildDetailMesh(mnavMemory* memory, const mnavCompactField* field,
                               const mnavRegionMap* regions, const mnavPolyMesh* mesh,
                               mnavDetailSettings settings, mnavDetailMesh* detail);

void mnavReleaseDetailMesh(mnavMemory* memory, mnavDetailMesh* detail);

#endif // MAUL_NAV_SRC_DETAIL_H
