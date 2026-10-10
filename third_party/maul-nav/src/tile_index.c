// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The triangles of a bake's meshes that may reach each tile of a grid
// (mnav-0014).

#include "tile_index.h"

#include "allocator.h"
#include "bake_def.h"
#include "input.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdint.h>

// Cells a triangle's bounds are widened by on each side, past the border:
// a tile's corner rounds by at most half a cell at the largest extent, so
// two keep every triangle the bake's own test lets reach a tile.
#define MARGIN_CELLS 2

static int64_t FloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

// The tiles, along one axis, that an input spanning low to high on that
// axis may reach.
static void TileRange(const mnavTileIndex* index, float low, float high, int64_t* lowOut,
                      int64_t* highOut)
{
    int64_t lowCell = (int64_t)floor((double)low / (double)index->cellSize) - MARGIN_CELLS;
    int64_t highCell = (int64_t)floor((double)high / (double)index->cellSize) + MARGIN_CELLS;
    *lowOut = FloorDiv(lowCell - index->border, index->tileCells);
    *highOut = FloorDiv(highCell + index->border, index->tileCells);
}

typedef struct Reach
{
    int64_t x0;
    int64_t x1;
    int64_t z0;
    int64_t z1;
} Reach;

static Reach TriangleReach(const mnavTileIndex* index, const mnavTriangleMesh* mesh, int32_t t)
{
    const int32_t* corner = mesh->indices + (size_t)t * 3;
    const mnavVec3* v = mesh->vertices;
    mnavVec3 a = v[corner[0]];
    mnavVec3 b = v[corner[1]];
    mnavVec3 c = v[corner[2]];
    Reach reach;
    TileRange(index, fminf(a.x, fminf(b.x, c.x)), fmaxf(a.x, fmaxf(b.x, c.x)), &reach.x0,
              &reach.x1);
    TileRange(index, fminf(a.z, fminf(b.z, c.z)), fmaxf(a.z, fmaxf(b.z, c.z)), &reach.z0,
              &reach.z1);
    return reach;
}

static Reach OutlineReach(const mnavTileIndex* index, const mnavOutline* outline)
{
    mnavVec2 low = outline->points[0];
    mnavVec2 high = low;
    for (int32_t i = 1; i < outline->pointCount; ++i)
    {
        low = (mnavVec2){fminf(low.x, outline->points[i].x), fminf(low.y, outline->points[i].y)};
        high = (mnavVec2){fmaxf(high.x, outline->points[i].x), fmaxf(high.y, outline->points[i].y)};
    }
    Reach reach;
    TileRange(index, low.x, high.x, &reach.x0, &reach.x1);
    TileRange(index, low.y, high.y, &reach.z0, &reach.z1);
    return reach;
}

// What an index lists: meshes' triangles, or outlines, each one item.
typedef struct Source
{
    const mnavTriangleMesh* meshes;
    const mnavOutline* outlines;
    int32_t count;
} Source;

static int32_t Items(Source s, int32_t g)
{
    return s.meshes != nullptr ? s.meshes[g].triangleCount : 1;
}

static Reach ItemReach(const mnavTileIndex* index, Source s, int32_t g, int32_t i)
{
    return s.meshes != nullptr ? TriangleReach(index, &s.meshes[g], i)
                               : OutlineReach(index, &s.outlines[g]);
}

// Finds the tiles the items reach and how many entries they make.
static mnavResult Measure(mnavTileIndex* index, Source s, int64_t* entriesOut)
{
    Reach all = {INT64_MAX, INT64_MIN, INT64_MAX, INT64_MIN};
    int64_t entries = 0;
    for (int32_t m = 0; m < s.count; ++m)
    {
        for (int32_t t = 0; t < Items(s, m); ++t)
        {
            Reach r = ItemReach(index, s, m, t);
            all.x0 = r.x0 < all.x0 ? r.x0 : all.x0;
            all.x1 = r.x1 > all.x1 ? r.x1 : all.x1;
            all.z0 = r.z0 < all.z0 ? r.z0 : all.z0;
            all.z1 = r.z1 > all.z1 ? r.z1 : all.z1;
            entries += (r.x1 - r.x0 + 1) * (r.z1 - r.z0 + 1);
            if (entries > INT32_MAX)
            {
                return mnav_errorLimit;
            }
        }
    }
    if (entries > 0)
    {
        // Inputs lie within the extent, so the grid's sides fit in 32 bits;
        // its tiles may not, and the grid is kept only when they do, so a
        // refused index stays 0 by 0 tiles.
        int64_t columns = all.x1 - all.x0 + 1;
        int64_t rows = all.z1 - all.z0 + 1;
        if (columns * rows >= INT32_MAX)
        {
            return mnav_errorLimit;
        }
        index->minX = (int32_t)all.x0;
        index->minZ = (int32_t)all.z0;
        index->columns = (int32_t)columns;
        index->rows = (int32_t)rows;
    }
    *entriesOut = entries;
    return mnav_success;
}

// Visits every tile each item reaches, items in input order: counting,
// then filling.
static void Fill(mnavTileIndex* index, Source s, bool count)
{
    for (int32_t m = 0; m < s.count; ++m)
    {
        for (int32_t t = 0; t < Items(s, m); ++t)
        {
            Reach r = ItemReach(index, s, m, t);
            for (int64_t z = r.z0; z <= r.z1; ++z)
            {
                for (int64_t x = r.x0; x <= r.x1; ++x)
                {
                    int64_t k = (x - index->minX) + (z - index->minZ) * index->columns;
                    if (count)
                    {
                        index->first[k + 1] += 1;
                    }
                    else
                    {
                        index->entries[index->first[k]++] = (mnavIndexEntry){m, t};
                    }
                }
            }
        }
    }
}

static void Release(mnavTileIndex* index)
{
    mnavMemory memory = index->memory;
    int32_t tiles = index->columns * index->rows;
    mnavRelease(&memory, index->entries, (size_t)index->entryCount, sizeof(mnavIndexEntry),
                alignof(mnavIndexEntry));
    mnavRelease(&memory, index->first, (size_t)tiles + 1, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, index->counts, (size_t)index->sourceCount, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(&memory, index, 1, sizeof(mnavTileIndex), alignof(mnavTileIndex));
}

static mnavResult Build(mnavTileIndex* index, Source s)
{
    mnavResult result = mnavAllocate(&index->memory, (size_t)s.count, sizeof(int32_t),
                                     alignof(int32_t), (void**)&index->counts);
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t m = 0; m < s.count; ++m)
    {
        index->counts[m] =
            s.meshes != nullptr ? s.meshes[m].triangleCount : s.outlines[m].pointCount;
    }
    int64_t entries = 0;
    result = Measure(index, s, &entries);
    int32_t tiles = index->columns * index->rows;
    if (result == mnav_success)
    {
        result = mnavAllocate(&index->memory, (size_t)tiles + 1, sizeof(int32_t), alignof(int32_t),
                              (void**)&index->first);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&index->memory, (size_t)entries, sizeof(mnavIndexEntry),
                              alignof(mnavIndexEntry), (void**)&index->entries);
        index->entryCount = result == mnav_success ? (int32_t)entries : 0;
    }
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t k = 0; k <= tiles; ++k)
    {
        index->first[k] = 0;
    }
    // Counts, sums them into each tile's end, fills each tile from its
    // start up to that end, then moves the ends back to starts.
    Fill(index, s, true);
    for (int32_t k = 0; k < tiles; ++k)
    {
        index->first[k + 1] += index->first[k];
    }
    Fill(index, s, false);
    for (int32_t k = tiles; k > 0; --k)
    {
        index->first[k] = index->first[k - 1];
    }
    index->first[0] = 0;
    return mnav_success;
}

// Makes an index of the checked input for the def's grid.
static mnavResult Make(const mnavBakeDef* def, const mnavBakeCells* cells, Source s,
                       mnavTileIndex** indexOut, mnavBakeReport* report)
{
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavTileIndex* index = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavTileIndex), alignof(mnavTileIndex), (void**)&index);
    if (result != mnav_success)
    {
        return result;
    }
    *index = (mnavTileIndex){.memory = memory,
                             .tileCells = def->tileCells,
                             .cellSize = def->cellSize,
                             .border = cells->border,
                             .outlines = s.outlines != nullptr,
                             .sourceCount = s.count};
    result = Build(index, s);
    if (result != mnav_success)
    {
        Release(index);
        return result;
    }
    report->memoryPeak = index->memory.peak;
    *indexOut = index;
    return mnav_success;
}

// A report cleared for an index's making: the input stage, no input
// refused.
static mnavBakeReport* Begin(mnavBakeReport* reportOut, mnavBakeReport* ignored)
{
    mnavBakeReport* report = reportOut != nullptr ? reportOut : ignored;
    *report = (mnavBakeReport){0};
    report->stage = mnav_stageInput;
    report->mesh = -1;
    report->input = (mnavInputResult){mnav_success, mnav_elementNone, -1};
    return report;
}

mnavResult mnavCreateTileIndex(const mnavBakeDef* def, const mnavTriangleMesh* meshes,
                               int32_t meshCount, mnavTileIndex** indexOut,
                               mnavBakeReport* reportOut)
{
    mnavBakeReport ignored;
    mnavBakeReport* report = Begin(reportOut, &ignored);
    if (indexOut == nullptr)
    {
        return report->result = mnav_errorInvalid;
    }
    *indexOut = nullptr;
    mnavBakeCells cells = {0};
    mnavResult checked = mnavCheckBakeDef(def, &cells).result;
    if (checked != mnav_success)
    {
        return report->result = checked;
    }
    if (meshCount < 0 || (meshCount > 0 && meshes == nullptr))
    {
        return report->result = mnav_errorInvalid;
    }
    for (int32_t m = 0; m < meshCount; ++m)
    {
        mnavInputResult input = mnavCheckTriangleMesh(def, &meshes[m]);
        if (input.result != mnav_success)
        {
            report->mesh = m;
            report->input = input;
            return report->result = input.result;
        }
    }
    return report->result =
               Make(def, &cells, (Source){meshes, nullptr, meshCount}, indexOut, report);
}

mnavResult mnavCreateTileIndex2D(const mnavBakeDef* def, const mnavOutline* outlines,
                                 int32_t outlineCount, mnavTileIndex** indexOut,
                                 mnavBakeReport* reportOut)
{
    mnavBakeReport ignored;
    mnavBakeReport* report = Begin(reportOut, &ignored);
    if (indexOut == nullptr)
    {
        return report->result = mnav_errorInvalid;
    }
    *indexOut = nullptr;
    mnavBakeCells cells = {0};
    mnavResult checked = mnavCheckBakeDef(def, &cells).result;
    if (checked != mnav_success)
    {
        return report->result = checked;
    }
    if (outlineCount < 0 || (outlineCount > 0 && outlines == nullptr))
    {
        return report->result = mnav_errorInvalid;
    }
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        mnavInputResult input = mnavCheckOutline(def, &outlines[o]);
        if (input.result != mnav_success)
        {
            report->mesh = o;
            report->input = input;
            return report->result = input.result;
        }
    }
    return report->result =
               Make(def, &cells, (Source){nullptr, outlines, outlineCount}, indexOut, report);
}

void mnavDestroyTileIndex(mnavTileIndex* index)
{
    if (index != nullptr)
    {
        Release(index);
    }
}

// Whether an index was made for the def's grid, of this kind of input and
// this many of it.
static bool FitsGrid(const mnavTileIndex* index, const mnavBakeDef* def, const mnavBakeCells* cells,
                     bool outlines, int32_t count)
{
    return index->tileCells == def->tileCells && index->cellSize == def->cellSize &&
           index->border == cells->border && index->outlines == outlines &&
           index->sourceCount == count;
}

bool mnavTileIndexFits(const mnavTileIndex* index, const mnavBakeDef* def,
                       const mnavBakeCells* cells, const mnavTriangleMesh* meshes,
                       int32_t meshCount)
{
    if (!FitsGrid(index, def, cells, false, meshCount))
    {
        return false;
    }
    for (int32_t m = 0; m < meshCount; ++m)
    {
        if (index->counts[m] != meshes[m].triangleCount)
        {
            return false;
        }
    }
    return true;
}

bool mnavTileIndexFits2D(const mnavTileIndex* index, const mnavBakeDef* def,
                         const mnavBakeCells* cells, const mnavOutline* outlines,
                         int32_t outlineCount)
{
    if (!FitsGrid(index, def, cells, true, outlineCount))
    {
        return false;
    }
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        if (index->counts[o] != outlines[o].pointCount)
        {
            return false;
        }
    }
    return true;
}

void mnavTileIndexList(const mnavTileIndex* index, int32_t tileX, int32_t tileZ,
                       const mnavIndexEntry** entriesOut, int32_t* countOut)
{
    int64_t x = (int64_t)tileX - index->minX;
    int64_t z = (int64_t)tileZ - index->minZ;
    if (x < 0 || z < 0 || x >= index->columns || z >= index->rows)
    {
        *entriesOut = nullptr;
        *countOut = 0;
        return;
    }
    int64_t k = x + z * index->columns;
    *entriesOut = index->entries + index->first[k];
    *countOut = index->first[k + 1] - index->first[k];
}
