// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's solid heightfield, merged from a sorted set of fragments.

#include "heightfield.h"

#include "allocator.h"
#include "invariant.h"
#include "outline.h"
#include "raster.h"
#include "terrain.h"
#include "tile_index.h"

#include "maul-nav/bake.h"

#include <string.h>

// Rasterizes one input triangle when it has area and touches the tile,
// counting it against the tile's limit.
static mnavResult AddTriangle(mnavMemory* memory, const mnavBakeDef* def,
                              const mnavBakeCells* cells, const mnavTileFrame* frame,
                              const mnavVec3 corners[3], mnavAreaType given, int32_t* touching,
                              mnavFragmentList* list)
{
    // The cheap test first: most triangles of a large mesh miss the tile.
    if (!mnavTriangleTouchesTile(frame, corners))
    {
        return mnav_success;
    }
    int32_t area = mnavTriangleArea(corners, given, cells->cosMaxSlope);
    if (area < 0)
    {
        return mnav_success;
    }
    if (++*touching > def->limits.tileTriangles)
    {
        return mnav_errorCapacity;
    }
    return mnavRasterizeTriangle(memory, frame, corners, (mnavAreaType)area, list);
}

// The cells of a terrain that may reach the tile, two triangles each.
static mnavResult AddTerrain(mnavMemory* memory, const mnavBakeDef* def, const mnavBakeCells* cells,
                             const mnavTileFrame* frame, const mnavTerrain* terrain,
                             int32_t* touching, mnavFragmentList* list)
{
    int32_t c0 = 0;
    int32_t c1 = -1;
    int32_t r0 = 0;
    int32_t r1 = -1;
    if (!mnavTerrainCells(terrain, frame, &c0, &c1, &r0, &r1))
    {
        return mnav_success;
    }
    for (int32_t r = r0; r <= r1; ++r)
    {
        for (int32_t c = c0; c <= c1; ++c)
        {
            for (int32_t k = 0; k < 2; ++k)
            {
                mnavVec3 corners[3];
                mnavAreaType given = 0;
                mnavResult result =
                    mnavTerrainTriangle(terrain, c, r, k, corners, &given)
                        ? AddTriangle(memory, def, cells, frame, corners, given, touching, list)
                        : mnav_success;
                if (result != mnav_success)
                {
                    return result;
                }
            }
        }
    }
    return mnav_success;
}

static mnavResult AddMeshTriangle(mnavMemory* memory, const mnavBakeDef* def,
                                  const mnavBakeCells* cells, const mnavTileFrame* frame,
                                  const mnavTriangleMesh* mesh, int32_t t, int32_t* touching,
                                  mnavFragmentList* list)
{
    const int32_t* index = mesh->indices + (size_t)t * 3;
    const mnavVec3 corners[3] = {mesh->vertices[index[0]], mesh->vertices[index[1]],
                                 mesh->vertices[index[2]]};
    mnavAreaType given = mesh->areas != nullptr ? mesh->areas[t] : mnav_areaWalkable;
    return AddTriangle(memory, def, cells, frame, corners, given, touching, list);
}

// Rasterizes the meshes' triangles that reach the tile: with a tile index,
// those it lists for the tile, which are all of them, in the same order.
static mnavResult CollectMeshes(mnavMemory* memory, const mnavBakeDef* def,
                                const mnavBakeCells* cells, const mnavBakeInput* input,
                                int32_t tileX, int32_t tileZ, const mnavTileFrame* frame,
                                int32_t* touching, mnavFragmentList* list)
{
    if (input->index != nullptr)
    {
        const mnavIndexEntry* entries = nullptr;
        int32_t count = 0;
        mnavTileIndexList(input->index, tileX, tileZ, &entries, &count);
        for (int32_t k = 0; k < count; ++k)
        {
            mnavResult result =
                AddMeshTriangle(memory, def, cells, frame, &input->meshes[entries[k].mesh],
                                entries[k].triangle, touching, list);
            if (result != mnav_success)
            {
                return result;
            }
        }
        return mnav_success;
    }
    for (int32_t m = 0; m < input->meshCount; ++m)
    {
        for (int32_t t = 0; t < input->meshes[m].triangleCount; ++t)
        {
            mnavResult result =
                AddMeshTriangle(memory, def, cells, frame, &input->meshes[m], t, touching, list);
            if (result != mnav_success)
            {
                return result;
            }
        }
    }
    return mnav_success;
}

static mnavResult Collect(mnavMemory* memory, const mnavBakeDef* def, const mnavBakeCells* cells,
                          const mnavBakeInput* input, int32_t tileX, int32_t tileZ,
                          const mnavTileFrame* frame, mnavFragmentList* list)
{
    int32_t touching = 0;
    mnavResult meshes =
        CollectMeshes(memory, def, cells, input, tileX, tileZ, frame, &touching, list);
    if (meshes != mnav_success)
    {
        return meshes;
    }
    for (int32_t i = 0; i < input->terrainCount; ++i)
    {
        mnavResult result =
            AddTerrain(memory, def, cells, frame, &input->terrains[i], &touching, list);
        if (result != mnav_success)
        {
            return result;
        }
    }
    return mnav_success;
}

static bool Before(const mnavFragment* a, const mnavFragment* b)
{
    if (a->bottom != b->bottom)
    {
        return a->bottom < b->bottom;
    }
    if (a->top != b->top)
    {
        return a->top < b->top;
    }
    return a->area < b->area;
}

// Orders fragments in place: by column, each column's region taking the
// fragments that belong to it by swaps, as in American flag sort, with a
// cursor per column in cursors; then each column by bottom, top and area.
// Fragments equal in all of those are equal in full, so the order is the
// same whatever the swaps did. columns receives each column's start.
static void Sort(mnavFragment* items, int32_t count, int32_t width, uint32_t* columns,
                 uint32_t* cursors)
{
    int32_t columnCount = width * width;
    memset(columns, 0, ((size_t)columnCount + 1) * sizeof(uint32_t));
    // No fragments, no array: every column is empty.
    if (count == 0 || items == nullptr)
    {
        return;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        columns[items[i].x + items[i].z * width + 1] += 1;
    }
    for (int32_t c = 0; c < columnCount; ++c)
    {
        columns[c + 1] += columns[c];
    }
    memcpy(cursors, columns, (size_t)columnCount * sizeof(uint32_t));
    for (int32_t c = 0; c < columnCount; ++c)
    {
        // In locals: a fragment's area is a char, which may alias the
        // cursors, so they would be read again after every store.
        uint32_t at = cursors[c];
        uint32_t stop = columns[c + 1];
        while (at < stop)
        {
            mnavFragment fragment = items[at];
            int32_t home = fragment.x + fragment.z * width;
            if (home == c)
            {
                at += 1;
                continue;
            }
            uint32_t to = cursors[home];
            cursors[home] = to + 1;
            items[at] = items[to];
            items[to] = fragment;
        }
        cursors[c] = at;
    }
    for (int32_t c = 0; c < columnCount; ++c)
    {
        uint32_t begin = columns[c];
        uint32_t stop = columns[c + 1];
        for (uint32_t i = begin + 1; i < stop; ++i)
        {
            mnavFragment item = items[i];
            uint32_t j = i;
            while (j > begin && Before(&item, &items[j - 1]))
            {
                items[j] = items[j - 1];
                --j;
            }
            items[j] = item;
        }
    }
}

// Merges one column's sorted fragments, first to end, into spans written
// from out; returns the number written.
static uint32_t MergeColumn(const mnavFragment* fragments, uint32_t first, uint32_t end,
                            int32_t step, mnavSpan* out)
{
    // Most columns hold one fragment: a run of one, whose top is its own.
    if (end == first + 1)
    {
        out[0] = (mnavSpan){fragments[first].bottom, fragments[first].top, fragments[first].area};
        return 1;
    }
    uint32_t written = 0;
    uint32_t i = first;
    while (i < end)
    {
        uint32_t runEnd = i + 1;
        uint16_t top = fragments[i].top;
        while (runEnd < end && fragments[runEnd].bottom <= top)
        {
            top = fragments[runEnd].top > top ? fragments[runEnd].top : top;
            ++runEnd;
        }
        mnavAreaType area = mnav_areaNone;
        for (uint32_t k = i; k < runEnd; ++k)
        {
            if ((int32_t)fragments[k].top + step >= (int32_t)top && fragments[k].area > area)
            {
                area = fragments[k].area;
            }
        }
        out[written++] = (mnavSpan){fragments[i].bottom, top, area};
        i = runEnd;
    }
    return written;
}

// Every column's spans ascend and are apart, each from its bottom up to its
// top, and the columns cover the spans in order.
static void CheckHeightfield(const mnavHeightfield* heightfield)
{
    int32_t columnCount = heightfield->frame.width * heightfield->frame.width;
    MNAV_ASSERT(heightfield->columns[0] == 0);
    MNAV_ASSERT(heightfield->columns[columnCount] == (uint32_t)heightfield->spanCount);
    MNAV_ASSERT(heightfield->spanCount <= heightfield->spanCapacity);
    for (int32_t c = 0; c < columnCount; ++c)
    {
        uint32_t end = heightfield->columns[c + 1];
        MNAV_ASSERT(heightfield->columns[c] <= end);
        for (uint32_t i = heightfield->columns[c]; i < end; ++i)
        {
            const mnavSpan* span = &heightfield->spans[i];
            MNAV_ASSERT(span->bottom < span->top);
            MNAV_ASSERT(i + 1 == end || span->top < heightfield->spans[i + 1].bottom);
            (void)span;
        }
    }
}

static mnavResult Merge(mnavMemory* memory, const mnavFragment* sorted, int32_t count, int32_t step,
                        mnavHeightfield* heightfield)
{
    mnavResult result = mnavAllocate(memory, (size_t)count, sizeof(mnavSpan), alignof(mnavSpan),
                                     (void**)&heightfield->spans);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t columnCount = heightfield->frame.width * heightfield->frame.width;
    uint32_t* columns = heightfield->columns;
    mnavSpan* spans = heightfield->spans;
    uint32_t written = 0;
    uint32_t first = columns[0];
    for (int32_t c = 0; c < columnCount; ++c)
    {
        uint32_t end = columns[c + 1];
        columns[c] = written;
        written += MergeColumn(sorted, first, end, step, spans + written);
        first = end;
    }
    columns[columnCount] = written;
    heightfield->spanCount = (int32_t)written;
    // The span array keeps the fragment count's size: merging only shrinks
    // and the bytes are released with the heightfield.
    heightfield->spanCapacity = count;
    if (MNAV_INVARIANTS)
    {
        CheckHeightfield(heightfield);
    }
    return mnav_success;
}

static mnavResult Finish(mnavMemory* memory, const mnavBakeCells* cells, mnavResult result,
                         mnavFragmentList* list, mnavHeightfield* heightfield);

mnavResult mnavBuildHeightfieldInput(mnavMemory* memory, const mnavBakeDef* def,
                                     const mnavBakeCells* cells, const mnavBakeInput* input,
                                     int32_t tileX, int32_t tileZ, mnavHeightfield* heightfield)
{
    *heightfield = (mnavHeightfield){0};
    // The baker refused a tile past the input's extent before any stage.
    bool framed = mnavMakeTileFrame(def, cells, tileX, tileZ, &heightfield->frame);
    MNAV_ASSERT(framed);
    (void)framed;
    mnavFragmentList list = {nullptr, 0, 0, def->limits.tileSpans};
    mnavResult result =
        Collect(memory, def, cells, input, tileX, tileZ, &heightfield->frame, &list);
    return Finish(memory, cells, result, &list, heightfield);
}

mnavResult mnavBuildHeightfield2D(mnavMemory* memory, const mnavBakeDef* def,
                                  const mnavBakeCells* cells, const mnavOutline* outlines,
                                  int32_t outlineCount, const mnavTileIndex* index, int32_t tileX,
                                  int32_t tileZ, mnavHeightfield* heightfield)
{
    *heightfield = (mnavHeightfield){0};
    bool framed = mnavMakeTileFrame(def, cells, tileX, tileZ, &heightfield->frame);
    MNAV_ASSERT(framed);
    (void)framed;
    mnavFragmentList list = {nullptr, 0, 0, def->limits.tileSpans};
    const mnavOutlineSet set = mnavOutlinesFor(outlines, outlineCount, index, tileX, tileZ);
    mnavResult result = mnavCollectOutlines(memory, def, &heightfield->frame, &set, &list);
    return Finish(memory, cells, result, &list, heightfield);
}

// Sorts and merges the collected fragments into spans, when collecting
// succeeded, and releases them; what the heightfield holds after a
// failure is its caller's to release.
static mnavResult Finish(mnavMemory* memory, const mnavBakeCells* cells, mnavResult result,
                         mnavFragmentList* list, mnavHeightfield* heightfield)
{
    int32_t width = heightfield->frame.width;
    size_t columnCount = (size_t)width * (size_t)width;
    uint32_t* cursors = nullptr;
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, columnCount + 1, sizeof(uint32_t), alignof(uint32_t),
                              (void**)&heightfield->columns);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, columnCount, sizeof(uint32_t), alignof(uint32_t),
                              (void**)&cursors);
    }
    if (result == mnav_success)
    {
        Sort(list->items, list->count, width, heightfield->columns, cursors);
        mnavRelease(memory, cursors, columnCount, sizeof(uint32_t), alignof(uint32_t));
        cursors = nullptr;
        result = Merge(memory, list->items, list->count, cells->agentStep, heightfield);
    }
    mnavRelease(memory, cursors, columnCount, sizeof(uint32_t), alignof(uint32_t));
    mnavReleaseFragments(memory, list);
    return result;
}

void mnavReleaseHeightfield(mnavMemory* memory, mnavHeightfield* heightfield)
{
    size_t columnCount = (size_t)heightfield->frame.width * (size_t)heightfield->frame.width + 1;
    if (heightfield->columns != nullptr)
    {
        mnavRelease(memory, heightfield->columns, columnCount, sizeof(uint32_t), alignof(uint32_t));
    }
    mnavRelease(memory, heightfield->spans, (size_t)heightfield->spanCapacity, sizeof(mnavSpan),
                alignof(mnavSpan));
    heightfield->columns = nullptr;
    heightfield->spans = nullptr;
    heightfield->spanCount = 0;
    heightfield->spanCapacity = 0;
}
