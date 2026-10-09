// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight volume's space (mnav-0015): descents of the compact octrees
// to a voxel and over a face's slab, and the conservative voxel walk.

#include "flight_space.h"

#include "flight.h"
#include "flight_volume.h"

#include "maul-nav/flight.h"

#include <math.h>
#include <stdint.h>

// The deepest a slab's descent goes: eight children on each of the at most
// seven node levels of a 512-voxel cube.
#define STACK_DEPTH 64

static int32_t FloorDiv(int32_t a, int32_t b)
{
    int32_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
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

mnavFlightCursor mnavMakeFlightCursor(const mnavFlightVolume* volume)
{
    int32_t side = volume->def.tileVoxels;
    return (mnavFlightCursor){volume, nullptr, 0, 0, false, side, volume->shape.cubeCount * side};
}

// The tile holding column (x, z), or NULL.
static const mnavFlightTile* TileOf(mnavFlightCursor* c, int32_t x, int32_t z)
{
    int32_t tileX = FloorDiv(x, c->side);
    int32_t tileZ = FloorDiv(z, c->side);
    if (!c->cached || tileX != c->tileX || tileZ != c->tileZ)
    {
        c->tile = mnavFlightTileAt(c->volume, tileX, tileZ);
        c->tileX = tileX;
        c->tileZ = tileZ;
        c->cached = true;
    }
    return c->tile;
}

// The child of a node's block at octant o: its index among the stored ones.
static uint32_t Child(const mnavFlightNode* node, int32_t o)
{
    return node->firstChild + Ones(node->mask & ((1u << o) - 1u));
}

// Descends tile t to voxel (x, y, z) of its own frame: what holds it, and
// the block holding it.
static int32_t Hold(const mnavFlightTile* t, int32_t x, int32_t y, int32_t z, mnavFlightBlock* b)
{
    int32_t c = y / t->side;
    *b = (mnavFlightBlock){0, c * t->side, 0, t->side};
    if (t->roots[c] != MNAV_FLIGHT_MIXED)
    {
        return t->roots[c] == MNAV_FLIGHT_SOLID ? MNAV_SPACE_SOLID : MNAV_SPACE_OPEN;
    }
    const mnavFlightNode* node = &t->nodes[t->rootNodes[c]];
    for (int32_t size = t->side / 2;; size /= 2)
    {
        int32_t o =
            (x >= b->x + size ? 1 : 0) | (y >= b->y + size ? 2 : 0) | (z >= b->z + size ? 4 : 0);
        b->x += (o & 1) != 0 ? size : 0;
        b->y += (o & 2) != 0 ? size : 0;
        b->z += (o & 4) != 0 ? size : 0;
        b->size = size;
        if ((node->mask >> (o + 8) & 1u) != 0)
        {
            return MNAV_SPACE_SOLID;
        }
        if ((node->mask >> o & 1u) == 0)
        {
            return MNAV_SPACE_OPEN;
        }
        uint32_t child = Child(node, o);
        if (size == 4)
        {
            int32_t bit = (x - b->x) + 4 * (y - b->y) + 16 * (z - b->z);
            *b = (mnavFlightBlock){x, y, z, 1};
            return (t->leaves[child] >> bit & 1u) != 0 ? MNAV_SPACE_SOLID : MNAV_SPACE_OPEN;
        }
        node = &t->nodes[child];
    }
}

int32_t mnavFlightHolder(mnavFlightCursor* cursor, int32_t x, int32_t y, int32_t z,
                         mnavFlightBlock* blockOut)
{
    if (y < 0 || y >= cursor->layers)
    {
        return MNAV_SPACE_SOLID;
    }
    const mnavFlightTile* t = TileOf(cursor, x, z);
    if (t == nullptr)
    {
        return MNAV_SPACE_UNLOADED;
    }
    int32_t ox = cursor->tileX * cursor->side;
    int32_t oz = cursor->tileZ * cursor->side;
    mnavFlightBlock b;
    int32_t held = Hold(t, x - ox, y, z - oz, &b);
    if (blockOut != nullptr)
    {
        *blockOut = (mnavFlightBlock){b.x + ox, b.y, b.z + oz, b.size};
    }
    return held;
}

// A region of a tile's frame, from lo up to but not including hi, and the
// tile's place in the volume's frame.
typedef struct Region
{
    int32_t lo[3];
    int32_t hi[3];
    int32_t ox;
    int32_t oz;
    mnavFlightVisit visit;
    void* context;
} Region;

static bool Meets(const Region* r, int32_t x, int32_t y, int32_t z, int32_t size)
{
    return x < r->hi[0] && y < r->hi[1] && z < r->hi[2] && x + size > r->lo[0] &&
           y + size > r->lo[1] && z + size > r->lo[2];
}

static void Visit(const Region* r, int32_t x, int32_t y, int32_t z, int32_t size)
{
    r->visit(r->context, (mnavFlightBlock){x + r->ox, y, z + r->oz, size});
}

// The bits of a leaf's voxels (bit x + 4y + 16z) within lo to hi on each
// axis, in the leaf's own frame, clamped to 0 to 4.
static uint64_t RegionBits(const int32_t lo[3], const int32_t hi[3])
{
    uint64_t row = 0;
    for (int32_t x = lo[0]; x < hi[0]; ++x)
    {
        row |= (uint64_t)1 << x;
    }
    uint64_t layer = 0;
    for (int32_t y = lo[1]; y < hi[1]; ++y)
    {
        layer |= row << (4 * y);
    }
    uint64_t bits = 0;
    for (int32_t z = lo[2]; z < hi[2]; ++z)
    {
        bits |= layer << (16 * z);
    }
    return bits;
}

// The index of a word's lowest set bit, by de Bruijn multiplication.
static int32_t LowestBit(uint64_t bits)
{
    static const int32_t table[64] = {
        0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,  62, 55, 59, 36, 53, 51,
        43, 22, 45, 39, 33, 30, 24, 18, 12, 5,  63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21,
        44, 32, 23, 11, 46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
    return table[((bits & (~bits + 1u)) * 0x03F79D71B4CB0A89ull) >> 58];
}

// Visits the open voxels of a leaf at (x, y, z) within the region, in
// the order of their bits.
static void VisitLeaf(const Region* r, uint64_t leaf, int32_t x, int32_t y, int32_t z)
{
    const int32_t at[3] = {x, y, z};
    int32_t lo[3];
    int32_t hi[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        lo[k] = r->lo[k] - at[k] < 0 ? 0 : r->lo[k] - at[k];
        hi[k] = r->hi[k] - at[k] > 4 ? 4 : r->hi[k] - at[k];
    }
    for (uint64_t open = ~leaf & RegionBits(lo, hi); open != 0; open &= open - 1u)
    {
        int32_t k = LowestBit(open);
        Visit(r, x + (k & 3), y + (k >> 2 & 3), z + (k >> 4), 1);
    }
}

// A mixed block still to search: its node and its lowest voxel and side.
typedef struct Frame
{
    uint32_t node;
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t size;
} Frame;

// Visits the open blocks of cube c of tile t within the region.
static void Collect(const mnavFlightTile* t, int32_t c, const Region* r)
{
    int32_t side = t->side;
    if (t->roots[c] != MNAV_FLIGHT_MIXED)
    {
        if (t->roots[c] == MNAV_FLIGHT_EMPTY)
        {
            Visit(r, 0, c * side, 0, side);
        }
        return;
    }
    Frame stack[STACK_DEPTH];
    int32_t top = 0;
    stack[top++] = (Frame){(uint32_t)t->rootNodes[c], 0, c * side, 0, side};
    while (top > 0)
    {
        Frame f = stack[--top];
        const mnavFlightNode* node = &t->nodes[f.node];
        int32_t h = f.size / 2;
        for (int32_t o = 0; o < 8; ++o)
        {
            int32_t x = f.x + ((o & 1) != 0 ? h : 0);
            int32_t y = f.y + ((o & 2) != 0 ? h : 0);
            int32_t z = f.z + ((o & 4) != 0 ? h : 0);
            if (!Meets(r, x, y, z, h) || (node->mask >> (o + 8) & 1u) != 0)
            {
                continue;
            }
            if ((node->mask >> o & 1u) == 0)
            {
                Visit(r, x, y, z, h);
            }
            else if (h == 4)
            {
                VisitLeaf(r, t->leaves[Child(node, o)], x, y, z);
            }
            else
            {
                stack[top++] = (Frame){Child(node, o), x, y, z, h};
            }
        }
    }
}

int32_t mnavFlightFace(mnavFlightCursor* cursor, const mnavFlightBlock* b, int32_t f,
                       mnavFlightVisit visit, void* context)
{
    int32_t axis = f / 2;
    Region r = {
        {b->x, b->y, b->z}, {b->x + b->size, b->y + b->size, b->z + b->size}, 0, 0, visit, context};
    r.lo[axis] = f % 2 != 0 ? r.hi[axis] : r.lo[axis] - 1;
    r.hi[axis] = r.lo[axis] + 1;
    if (r.lo[1] < 0 || r.hi[1] > cursor->layers)
    {
        return MNAV_SPACE_SOLID;
    }
    // The slab lies in one tile and one cube: a block never crosses
    // either, and across a side face the slab spans the block's own rows.
    const mnavFlightTile* t = TileOf(cursor, r.lo[0], r.lo[2]);
    if (t == nullptr)
    {
        return MNAV_SPACE_UNLOADED;
    }
    r.ox = cursor->tileX * cursor->side;
    r.oz = cursor->tileZ * cursor->side;
    r.lo[0] -= r.ox;
    r.hi[0] -= r.ox;
    r.lo[2] -= r.oz;
    r.hi[2] -= r.oz;
    Collect(t, r.lo[1] / cursor->side, &r);
    return MNAV_SPACE_OPEN;
}

// Where the walk stands: its voxel, its steps, the fraction of the
// segment at which it next crosses each axis, and the last open block it
// was in (side 0 for none). Every voxel the walk touches while it stays
// in that block is the block's, open, so it is looked up no more.
typedef struct Walk
{
    int32_t at[3];
    int32_t step[3];
    double next[3];
    double delta[3];
    mnavFlightBlock open;
} Walk;

static bool Inside(const mnavFlightBlock* b, const int32_t at[3])
{
    return at[0] >= b->x && at[1] >= b->y && at[2] >= b->z && at[0] < b->x + b->size &&
           at[1] < b->y + b->size && at[2] < b->z + b->size;
}

// What holds the walk's voxel, keeping the open block that holds it.
static int32_t Look(mnavFlightCursor* cursor, Walk* w)
{
    if (Inside(&w->open, w->at))
    {
        return MNAV_SPACE_OPEN;
    }
    int32_t held = mnavFlightHolder(cursor, w->at[0], w->at[1], w->at[2], &w->open);
    w->open.size = held == MNAV_SPACE_OPEN ? w->open.size : 0;
    return held;
}

// What holds the voxels the walk touches as it crosses the edge or corner
// between the axes in tied at once: each one reached by a part of the
// steps.
static int32_t Corners(mnavFlightCursor* cursor, const Walk* w, uint32_t tied)
{
    for (uint32_t m = 1; m < 7; ++m)
    {
        if ((m & ~tied) != 0 || m == tied)
        {
            continue;
        }
        int32_t p[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            p[k] = w->at[k] + ((m >> k & 1u) != 0 ? w->step[k] : 0);
        }
        int32_t held = mnavFlightHolder(cursor, p[0], p[1], p[2], nullptr);
        if (held != MNAV_SPACE_OPEN)
        {
            return held;
        }
    }
    return MNAV_SPACE_OPEN;
}

static Walk StartWalk(const double a[3], const double b[3])
{
    Walk w;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = b[k] - a[k];
        w.at[k] = (int32_t)floor(a[k]);
        w.step[k] = d > 0.0 ? 1 : (d < 0.0 ? -1 : 0);
        double edge = w.step[k] > 0 ? (double)w.at[k] + 1.0 : (double)w.at[k];
        w.next[k] = w.step[k] != 0 ? (edge - a[k]) / d : (double)INFINITY;
        w.delta[k] = w.step[k] != 0 ? fabs(1.0 / d) : (double)INFINITY;
    }
    w.open = (mnavFlightBlock){0, 0, 0, 0};
    return w;
}

// Moves the walk on at fraction t across every axis it crosses there;
// returns what holds the first voxel it touches on the way that is not
// open, crossing an edge or a corner.
static int32_t Advance(mnavFlightCursor* cursor, Walk* w, double t)
{
    uint32_t tied = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        tied |= w->next[k] - t < 1e-9 ? 1u << k : 0u;
    }
    int32_t to[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        to[k] = w->at[k] + ((tied >> k & 1u) != 0 ? w->step[k] : 0);
    }
    // Every voxel touched on the way lies in the box between the voxels
    // left and entered: inside the open block when both are.
    bool within = Inside(&w->open, to);
    int32_t held =
        (tied & (tied - 1u)) != 0 && !within ? Corners(cursor, w, tied) : MNAV_SPACE_OPEN;
    for (int32_t k = 0; k < 3; ++k)
    {
        bool crossed = (tied >> k & 1u) != 0;
        w->at[k] = to[k];
        w->next[k] += crossed ? w->delta[k] : 0.0;
    }
    return held;
}

int32_t mnavFlightWalk(mnavFlightCursor* cursor, const double a[3], const double b[3], double* tOut)
{
    Walk w = StartWalk(a, b);
    const int32_t end[3] = {(int32_t)floor(b[0]), (int32_t)floor(b[1]), (int32_t)floor(b[2])};
    double t = 0.0;
    int32_t held = Look(cursor, &w);
    while (held == MNAV_SPACE_OPEN && (w.at[0] != end[0] || w.at[1] != end[1] || w.at[2] != end[2]))
    {
        t = fmin(w.next[0], fmin(w.next[1], w.next[2]));
        if (t > 1.0)
        {
            return MNAV_SPACE_OPEN;
        }
        held = Advance(cursor, &w, t);
        held = held == MNAV_SPACE_OPEN ? Look(cursor, &w) : held;
    }
    if (held != MNAV_SPACE_OPEN && tOut != nullptr)
    {
        *tOut = t;
    }
    return held;
}
