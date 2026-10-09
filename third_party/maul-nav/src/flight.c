// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flight volume tiles (mnav-0015): the bake's input rasterized at the
// voxel size by the heightfield, kept as a bitset per column of voxels
// with a border as wide as the flier's radius, solid below the ground
// when asked, dilated by a ball of that radius, then made into a compact
// octree per cube of the tile's side.

#include "flight.h"

#include "allocator.h"
#include "bake_def.h"
#include "flight_def.h"
#include "heightfield.h"
#include "raster.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// The voxels of a tile and its border: width columns on a side, each a
// bitset of words 64-bit words, bit y of a column voxel y up from the
// floor voxel.
typedef struct Grid
{
    uint64_t* bits;
    int32_t width;
    int32_t layers;
    int32_t words;
} Grid;

static uint64_t* Column(const Grid* g, int32_t x, int32_t z)
{
    return &g->bits[((size_t)z * (size_t)g->width + (size_t)x) * (size_t)g->words];
}

// Sets bits from (inclusive) to to (exclusive) of a column, clamped to
// its layers.
static void SetRange(const Grid* g, uint64_t* column, int32_t from, int32_t to)
{
    from = from < 0 ? 0 : from;
    to = to > g->layers ? g->layers : to;
    for (int32_t y = from; y < to; ++y)
    {
        column[y / 64] |= (uint64_t)1 << (y % 64);
    }
}

static bool Get(const uint64_t* column, int32_t y)
{
    return (column[y / 64] >> (y % 64) & 1u) != 0;
}

// Marks the heightfield's spans, offset columns in, and below the lowest
// span of each column with ground when asked.
static void Mark(const mnavHeightfield* hf, int32_t offset, int32_t floorVoxel, bool groundBelow,
                 const Grid* g)
{
    for (int32_t z = 0; z < g->width; ++z)
    {
        for (int32_t x = 0; x < g->width; ++x)
        {
            uint32_t at =
                (uint32_t)(x + offset) + (uint32_t)(z + offset) * (uint32_t)hf->frame.width;
            uint64_t* column = Column(g, x, z);
            uint32_t first = hf->columns[at];
            uint32_t last = hf->columns[at + 1];
            for (uint32_t s = first; s < last; ++s)
            {
                int32_t bottom = (int32_t)hf->spans[s].bottom - MNAV_HEIGHT_OFFSET - floorVoxel;
                int32_t top = (int32_t)hf->spans[s].top - MNAV_HEIGHT_OFFSET - floorVoxel;
                SetRange(g, column, bottom, top);
            }
            if (groundBelow && first < last)
            {
                SetRange(g, column, 0,
                         (int32_t)hf->spans[first].bottom - MNAV_HEIGHT_OFFSET - floorVoxel);
            }
        }
    }
}

// ORs column from, every bit spread h layers up and down, into column to.
static void Spread(const Grid* g, const uint64_t* from, uint64_t* to, int32_t h)
{
    for (int32_t y = 0; y < g->layers; ++y)
    {
        if (!Get(from, y))
        {
            continue;
        }
        SetRange(g, to, y - h, y + h + 1);
    }
}

// The dilation by a ball of r voxels: a voxel is solid in out when one
// of in lies within r of it, by whole offsets with dx² + dy² + dz² <= r².
static void Dilate(const Grid* in, const Grid* out, int32_t r)
{
    for (int32_t dz = -r; dz <= r; ++dz)
    {
        for (int32_t dx = -r; dx <= r; ++dx)
        {
            int32_t rest = r * r - dx * dx - dz * dz;
            if (rest < 0)
            {
                continue;
            }
            int32_t h = 0;
            while ((h + 1) * (h + 1) <= rest)
            {
                ++h;
            }
            for (int32_t z = 0; z < in->width; ++z)
            {
                for (int32_t x = 0; x < in->width; ++x)
                {
                    int32_t tx = x + dx;
                    int32_t tz = z + dz;
                    if (tx >= 0 && tz >= 0 && tx < in->width && tz < in->width)
                    {
                        Spread(in, Column(in, x, z), Column(out, tx, tz), h);
                    }
                }
            }
        }
    }
}

// The classes of one cube's blocks, level by level: leaf values for the
// 4-voxel blocks, then a class for each block of 8, 16, ... side voxels,
// each level a dense array of (side / size)³ by x + n (y + n z).
typedef struct Levels
{
    uint64_t* leaves;
    uint8_t* classes[8];
    int32_t count;
} Levels;

static uint8_t LeafClass(uint64_t value)
{
    return value == 0 ? MNAV_FLIGHT_EMPTY
                      : (value == UINT64_MAX ? MNAV_FLIGHT_SOLID : MNAV_FLIGHT_MIXED);
}

// The leaf of block (bx, by, bz) of cube c, read from the grid past its
// border of r columns.
static uint64_t LeafAt(const Grid* g, int32_t r, int32_t side, int32_t c, int32_t bx, int32_t by,
                       int32_t bz)
{
    uint64_t value = 0;
    for (int32_t k = 0; k < 4; ++k)
    {
        for (int32_t i = 0; i < 4; ++i)
        {
            const uint64_t* column = Column(g, r + bx * 4 + i, r + bz * 4 + k);
            for (int32_t j = 0; j < 4; ++j)
            {
                bool solid = Get(column, c * side + by * 4 + j);
                value |= (uint64_t)(solid ? 1 : 0) << (i + 4 * j + 16 * k);
            }
        }
    }
    return value;
}

// The leaves of cube c.
static void ReadLeaves(const Grid* g, int32_t r, int32_t side, int32_t c, uint64_t* leaves)
{
    int32_t n = side / 4;
    for (int32_t bz = 0; bz < n; ++bz)
    {
        for (int32_t by = 0; by < n; ++by)
        {
            for (int32_t bx = 0; bx < n; ++bx)
            {
                leaves[bx + n * (by + n * bz)] = LeafAt(g, r, side, c, bx, by, bz);
            }
        }
    }
}

// The class of child o of block (x, y, z) one level below level l: a leaf
// below level 0.
static uint8_t ChildClass(const Levels* v, int32_t l, int32_t n, int32_t x, int32_t y, int32_t z,
                          int32_t o)
{
    int32_t m = n * 2;
    int32_t cx = x * 2 + (o & 1);
    int32_t cy = y * 2 + (o >> 1 & 1);
    int32_t cz = z * 2 + (o >> 2 & 1);
    int32_t at = cx + m * (cy + m * cz);
    return l == 0 ? LeafClass(v->leaves[at]) : v->classes[l - 1][at];
}

// The class of block (x, y, z) of level l, n blocks a side, from its
// eight children's.
static uint8_t BlockClass(const Levels* v, int32_t l, int32_t n, int32_t x, int32_t y, int32_t z)
{
    int32_t empty = 0;
    int32_t solid = 0;
    for (int32_t o = 0; o < 8; ++o)
    {
        uint8_t c = ChildClass(v, l, n, x, y, z, o);
        empty += c == MNAV_FLIGHT_EMPTY ? 1 : 0;
        solid += c == MNAV_FLIGHT_SOLID ? 1 : 0;
    }
    return empty == 8 ? MNAV_FLIGHT_EMPTY : (solid == 8 ? MNAV_FLIGHT_SOLID : MNAV_FLIGHT_MIXED);
}

// Classes every block of levels 8 voxels and up from the leaves.
static void Classify(Levels* v, int32_t side)
{
    for (int32_t l = 0; l < v->count; ++l)
    {
        int32_t n = side >> (l + 3);
        for (int32_t z = 0; z < n; ++z)
        {
            for (int32_t y = 0; y < n; ++y)
            {
                for (int32_t x = 0; x < n; ++x)
                {
                    v->classes[l][x + n * (y + n * z)] = BlockClass(v, l, n, x, y, z);
                }
            }
        }
    }
}

// The index of child o of block (x, y, z) in a level of n blocks a side,
// in the level below, of 2n.
static int32_t ChildAt(int32_t n, int32_t x, int32_t y, int32_t z, int32_t o)
{
    int32_t m = n * 2;
    return (x * 2 + (o & 1)) + m * ((y * 2 + (o >> 1 & 1)) + m * (z * 2 + (o >> 2 & 1)));
}

// Grows the tile's nodes and leaves to hold needed of each.
static mnavResult Room(mnavMemory* memory, const mnavFlightLimits* limits, mnavFlightTile* t,
                       int32_t nodes, int32_t leaves)
{
    if (nodes > limits->tileNodes || leaves > limits->tileLeaves)
    {
        return mnav_errorLimit;
    }
    mnavResult r = mnavReserve(memory, (void**)&t->nodes, &t->nodeCapacity, t->nodeCount, nodes,
                               sizeof(mnavFlightNode), alignof(mnavFlightNode));
    return r == mnav_success
               ? mnavReserve(memory, (void**)&t->leaves, &t->leafCapacity, t->leafCount, leaves,
                             sizeof(uint64_t), alignof(uint64_t))
               : r;
}

// Appends one cube's nodes, level by level from its root, and the mixed
// leaves below them. cur and next hold three coordinates per block, room
// for the deepest level's blocks.
static mnavResult Emit(mnavMemory* memory, const mnavFlightLimits* limits, const Levels* v,
                       int32_t* cur, int32_t* next, mnavFlightTile* t)
{
    int32_t count = 1;
    cur[0] = 0;
    cur[1] = 0;
    cur[2] = 0;
    // The root's level has one block a side, each level below twice as many.
    int32_t n = 1;
    for (int32_t l = v->count - 1; l >= 0; --l, n *= 2)
    {
        int32_t base = t->nodeCount;
        mnavResult r = Room(memory, limits, t, base + count, t->leafCount + count * 8);
        if (r != mnav_success)
        {
            return r;
        }
        int32_t nextCount = 0;
        for (int32_t i = 0; i < count; ++i)
        {
            int32_t x = cur[3 * i];
            int32_t y = cur[3 * i + 1];
            int32_t z = cur[3 * i + 2];
            mnavFlightNode node = {
                l == 0 ? (uint32_t)t->leafCount : (uint32_t)(base + count + nextCount), 0};
            for (int32_t o = 0; o < 8; ++o)
            {
                uint8_t c = ChildClass(v, l, n, x, y, z, o);
                node.mask |= (uint16_t)(c == MNAV_FLIGHT_SOLID ? 1u << (o + 8) : 0u);
                if (c != MNAV_FLIGHT_MIXED)
                {
                    continue;
                }
                node.mask |= (uint16_t)(1u << o);
                if (l == 0)
                {
                    t->leaves[t->leafCount++] = v->leaves[ChildAt(n, x, y, z, o)];
                    continue;
                }
                next[3 * nextCount] = x * 2 + (o & 1);
                next[3 * nextCount + 1] = y * 2 + (o >> 1 & 1);
                next[3 * nextCount + 2] = z * 2 + (o >> 2 & 1);
                ++nextCount;
            }
            t->nodes[t->nodeCount++] = node;
        }
        int32_t* swap = cur;
        cur = next;
        next = swap;
        count = nextCount;
    }
    return mnav_success;
}

static size_t GridWords(const Grid* g)
{
    return (size_t)g->width * (size_t)g->width * (size_t)g->words;
}

static mnavResult MakeGrid(mnavMemory* memory, Grid* g)
{
    mnavResult r =
        mnavAllocate(memory, GridWords(g), sizeof(uint64_t), alignof(uint64_t), (void**)&g->bits);
    if (r == mnav_success)
    {
        memset(g->bits, 0, GridWords(g) * sizeof(uint64_t));
    }
    return r;
}

static void DropGrid(mnavMemory* memory, Grid* g)
{
    mnavRelease(memory, g->bits, GridWords(g), sizeof(uint64_t), alignof(uint64_t));
    g->bits = nullptr;
}

// What a tile is built from: the heightfield's def and cells at the
// voxel size, the input, the tile's place and floor voxel, and whether
// the ground's columns are solid below.
typedef struct Build
{
    const mnavBakeDef* def;
    const mnavBakeCells* cells;
    const mnavBakeInput* input;
    int32_t tileX;
    int32_t tileZ;
    int32_t floorVoxel;
    bool groundBelow;
} Build;

// The tile's voxels with a border of the radius's columns, dilated: the
// heightfield at the voxel size, marked, then spread by a ball of the
// radius. spansOut receives the heightfield's spans.
static mnavResult Voxels(mnavMemory* memory, const Build* b, Grid* out, int32_t* spansOut)
{
    // A tile index lists triangles for the navmesh's tiles and border,
    // not the volume's: every triangle is read.
    mnavBakeInput all = *b->input;
    all.index = nullptr;
    mnavHeightfield hf = {0};
    mnavResult r =
        mnavBuildHeightfieldInput(memory, b->def, b->cells, &all, b->tileX, b->tileZ, &hf);
    Grid raw = *out;
    r = r == mnav_success ? MakeGrid(memory, &raw) : r;
    if (r == mnav_success)
    {
        *spansOut = hf.spanCount;
        Mark(&hf, b->cells->border - b->cells->agentRadius, b->floorVoxel, b->groundBelow, &raw);
    }
    mnavReleaseHeightfield(memory, &hf);
    r = r == mnav_success ? MakeGrid(memory, out) : r;
    if (r == mnav_success)
    {
        Dilate(&raw, out, b->cells->agentRadius);
    }
    DropGrid(memory, &raw);
    return r;
}

// The memory one cube's levels and block lists take: the leaves, a class
// per block of each level, and two lists of the deepest level's blocks.
typedef struct Scratch
{
    Levels levels;
    int32_t* lists[2];
    int32_t side;
} Scratch;

static int64_t Blocks(int32_t n)
{
    return (int64_t)n * n * n;
}

static void DropScratch(mnavMemory* memory, Scratch* s)
{
    int32_t side = s->side;
    mnavRelease(memory, s->levels.leaves, (size_t)Blocks(side / 4), sizeof(uint64_t),
                alignof(uint64_t));
    for (int32_t l = 0; l < s->levels.count; ++l)
    {
        mnavRelease(memory, s->levels.classes[l], (size_t)Blocks(side >> (l + 3)), 1, 1);
    }
    for (int32_t k = 0; k < 2; ++k)
    {
        mnavRelease(memory, s->lists[k], (size_t)Blocks(side / 8) * 3, sizeof(int32_t),
                    alignof(int32_t));
    }
}

static mnavResult MakeScratch(mnavMemory* memory, int32_t side, Scratch* s)
{
    *s = (Scratch){.side = side};
    while ((side >> (s->levels.count + 3)) >= 1)
    {
        ++s->levels.count;
    }
    mnavResult r = mnavAllocate(memory, (size_t)Blocks(side / 4), sizeof(uint64_t),
                                alignof(uint64_t), (void**)&s->levels.leaves);
    for (int32_t l = 0; l < s->levels.count && r == mnav_success; ++l)
    {
        r = mnavAllocate(memory, (size_t)Blocks(side >> (l + 3)), 1, 1,
                         (void**)&s->levels.classes[l]);
    }
    for (int32_t k = 0; k < 2 && r == mnav_success; ++k)
    {
        r = mnavAllocate(memory, (size_t)Blocks(side / 8) * 3, sizeof(int32_t), alignof(int32_t),
                         (void**)&s->lists[k]);
    }
    return r;
}

// Each cube's octree from the dilated grid, its border r columns wide.
static mnavResult Cubes(mnavMemory* memory, const mnavFlightLimits* limits, const Grid* g,
                        int32_t r, mnavFlightTile* t)
{
    Scratch s;
    mnavResult result = MakeScratch(memory, t->side, &s);
    for (int32_t c = 0; c < t->cubeCount && result == mnav_success; ++c)
    {
        ReadLeaves(g, r, t->side, c, s.levels.leaves);
        Classify(&s.levels, t->side);
        uint8_t root = s.levels.classes[s.levels.count - 1][0];
        t->roots[c] = root;
        t->rootNodes[c] = root == MNAV_FLIGHT_MIXED ? t->nodeCount : -1;
        if (root == MNAV_FLIGHT_MIXED)
        {
            result = Emit(memory, limits, &s.levels, s.lists[0], s.lists[1], t);
        }
    }
    DropScratch(memory, &s);
    return result;
}

mnavResult mnavBuildFlightTile(mnavMemory* memory, const mnavFlightDef* def,
                               const mnavFlightShape* shape, const mnavBakeInput* input,
                               int32_t tileX, int32_t tileZ, mnavFlightTile* tileOut,
                               int32_t* spansOut)
{
    mnavFlightTile* t = tileOut;
    *t = (mnavFlightTile){.tileX = tileX,
                          .tileZ = tileZ,
                          .side = def->tileVoxels,
                          .floorVoxel = shape->floorVoxel,
                          .cubeCount = shape->cubeCount};
    *spansOut = 0;
    mnavBakeDef voxels = mnavFlightBakeDef(def);
    mnavBakeCells cells;
    if (mnavValidateBakeDef(&voxels, &cells).result != mnav_success)
    {
        return mnav_errorInvalid;
    }
    const Build b = {&voxels, &cells, input, tileX, tileZ, t->floorVoxel, def->groundBelow};
    int32_t layers = t->cubeCount * t->side;
    Grid g = {nullptr, t->side + 2 * cells.agentRadius, layers, (layers + 63) / 64};
    mnavResult r = Voxels(memory, &b, &g, spansOut);
    r = r == mnav_success ? mnavAllocate(memory, (size_t)t->cubeCount, 1, 1, (void**)&t->roots) : r;
    r = r == mnav_success ? mnavAllocate(memory, (size_t)t->cubeCount, sizeof(int32_t),
                                         alignof(int32_t), (void**)&t->rootNodes)
                          : r;
    r = r == mnav_success ? Cubes(memory, &def->limits, &g, cells.agentRadius, t) : r;
    DropGrid(memory, &g);
    if (r != mnav_success)
    {
        mnavReleaseFlightTile(memory, t);
    }
    return r;
}

// The bits set in a mask.
static uint32_t Ones(uint32_t mask)
{
    uint32_t count = 0;
    for (; mask != 0; mask &= mask - 1u)
    {
        ++count;
    }
    return count;
}

bool mnavFlightSolid(const mnavFlightTile* tile, int32_t x, int32_t y, int32_t z)
{
    int32_t c = y / tile->side;
    if (tile->roots[c] != MNAV_FLIGHT_MIXED)
    {
        return tile->roots[c] == MNAV_FLIGHT_SOLID;
    }
    y -= c * tile->side;
    const mnavFlightNode* node = &tile->nodes[tile->rootNodes[c]];
    for (int32_t size = tile->side / 2;; size /= 2)
    {
        int32_t o = (x >= size ? 1 : 0) | (y >= size ? 2 : 0) | (z >= size ? 4 : 0);
        x -= x >= size ? size : 0;
        y -= y >= size ? size : 0;
        z -= z >= size ? size : 0;
        if ((node->mask >> (o + 8) & 1u) != 0)
        {
            return true;
        }
        if ((node->mask >> o & 1u) == 0)
        {
            return false;
        }
        uint32_t child = node->firstChild + Ones(node->mask & ((1u << o) - 1u));
        if (size == 4)
        {
            return (tile->leaves[child] >> (x + 4 * y + 16 * z) & 1u) != 0;
        }
        node = &tile->nodes[child];
    }
}

void mnavReleaseFlightTile(mnavMemory* memory, mnavFlightTile* tile)
{
    mnavRelease(memory, tile->roots, (size_t)tile->cubeCount, 1, 1);
    mnavRelease(memory, tile->rootNodes, (size_t)tile->cubeCount, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(memory, tile->nodes, (size_t)tile->nodeCapacity, sizeof(mnavFlightNode),
                alignof(mnavFlightNode));
    mnavRelease(memory, tile->leaves, (size_t)tile->leafCapacity, sizeof(uint64_t),
                alignof(uint64_t));
    *tile = (mnavFlightTile){0};
}
