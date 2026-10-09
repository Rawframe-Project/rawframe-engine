// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fingerprint of a tile's input (mnav-0003), which the navmesh and
// flight volume bakers both write into their tiles.

#include "fingerprint.h"

#include "bytes.h"
#include "outline.h"
#include "raster.h"
#include "terrain.h"
#include "tile_index.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <stdint.h>

uint64_t mnavHashWords(uint64_t hash, const uint32_t* words, int32_t count)
{
    return mnavHash64(hash, words, count * (int32_t)sizeof(uint32_t));
}

// Adds a triangle that reaches the tile, as rasterization picks it, to
// the fingerprint, and counts it.
static uint64_t HashTriangle(float cosMaxSlope, const mnavTileFrame* frame,
                             const mnavVec3 corners[3], mnavAreaType given, uint64_t hash,
                             int32_t* count)
{
    // The cheap test first: most triangles of a large mesh miss the tile.
    if (!mnavTriangleTouchesTile(frame, corners))
    {
        return hash;
    }
    int32_t area = mnavTriangleArea(corners, given, cosMaxSlope);
    if (area < 0)
    {
        return hash;
    }
    uint32_t words[10];
    for (int32_t c = 0; c < 3; ++c)
    {
        words[c * 3 + 0] = mnavFloatBits(corners[c].x);
        words[c * 3 + 1] = mnavFloatBits(corners[c].y);
        words[c * 3 + 2] = mnavFloatBits(corners[c].z);
    }
    words[9] = (uint32_t)area;
    *count += 1;
    return mnavHashWords(hash, words, 10);
}

static uint64_t HashTerrain(float cosMaxSlope, const mnavTileFrame* frame,
                            const mnavTerrain* terrain, uint64_t hash, int32_t* count)
{
    int32_t c0 = 0;
    int32_t c1 = -1;
    int32_t r0 = 0;
    int32_t r1 = -1;
    if (!mnavTerrainCells(terrain, frame, &c0, &c1, &r0, &r1))
    {
        return hash;
    }
    for (int32_t r = r0; r <= r1; ++r)
    {
        for (int32_t c = c0; c <= c1; ++c)
        {
            for (int32_t k = 0; k < 2; ++k)
            {
                mnavVec3 corners[3];
                mnavAreaType given = 0;
                if (mnavTerrainTriangle(terrain, c, r, k, corners, &given))
                {
                    hash = HashTriangle(cosMaxSlope, frame, corners, given, hash, count);
                }
            }
        }
    }
    return hash;
}

// Adds the volumes that reach the tile to the fingerprint, after a word
// for include volumes, which reach every tile, when there are any; a bake
// whose volumes all miss the tile, none of them include volumes, has the
// fingerprint of one without.
static uint64_t HashVolumes(const mnavTileFrame* frame, const mnavBakeVolume* volumes,
                            int32_t count, uint64_t hash)
{
    if (count == 0)
    {
        return hash;
    }
    uint32_t includes = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        includes |= volumes[i].kind == mnav_volumeInclude ? 1u : 0u;
    }
    hash = includes != 0 ? mnavHashWords(hash, &includes, 1) : hash;
    for (int32_t i = 0; i < count; ++i)
    {
        const mnavBakeVolume* v = &volumes[i];
        if (!mnavRingTouchesTile(frame, v->points, v->pointCount))
        {
            continue;
        }
        const uint32_t head[5] = {(uint32_t)v->kind, (uint32_t)v->area, mnavFloatBits(v->minY),
                                  mnavFloatBits(v->maxY), (uint32_t)v->pointCount};
        hash = mnavHashWords(hash, head, 5);
        for (int32_t p = 0; p < v->pointCount; ++p)
        {
            const uint32_t point[2] = {mnavFloatBits(v->points[p].x),
                                       mnavFloatBits(v->points[p].y)};
            hash = mnavHashWords(hash, point, 2);
        }
    }
    return hash;
}

// Adds every input triangle that reaches the tile, as rasterization picks
// them, meshes' then terrains', and the volumes to the fingerprint, and
// counts the triangles.
static uint64_t HashMeshTriangle(float cosMaxSlope, const mnavTileFrame* frame,
                                 const mnavTriangleMesh* mesh, int32_t t, uint64_t hash,
                                 int32_t* count)
{
    const int32_t* index = mesh->indices + (size_t)t * 3;
    const mnavVec3 corners[3] = {mesh->vertices[index[0]], mesh->vertices[index[1]],
                                 mesh->vertices[index[2]]};
    mnavAreaType given = mesh->areas != nullptr ? mesh->areas[t] : mnav_areaWalkable;
    return HashTriangle(cosMaxSlope, frame, corners, given, hash, count);
}

uint64_t mnavFingerprintInput(const mnavTileFrame* frame, float cosMaxSlope,
                              const mnavBakeInput* input, int32_t tileX, int32_t tileZ,
                              uint64_t hash, int32_t* count)
{
    *count = 0;
    if (input->index != nullptr)
    {
        const mnavIndexEntry* list = nullptr;
        int32_t listed = 0;
        mnavTileIndexList(input->index, tileX, tileZ, &list, &listed);
        for (int32_t k = 0; k < listed; ++k)
        {
            hash = HashMeshTriangle(cosMaxSlope, frame, &input->meshes[list[k].mesh],
                                    list[k].triangle, hash, count);
        }
    }
    for (int32_t m = 0; m < input->meshCount && input->index == nullptr; ++m)
    {
        for (int32_t t = 0; t < input->meshes[m].triangleCount; ++t)
        {
            hash = HashMeshTriangle(cosMaxSlope, frame, &input->meshes[m], t, hash, count);
        }
    }
    for (int32_t i = 0; i < input->terrainCount; ++i)
    {
        hash = HashTerrain(cosMaxSlope, frame, &input->terrains[i], hash, count);
    }
    return HashVolumes(frame, input->volumes, input->volumeCount, hash);
}
