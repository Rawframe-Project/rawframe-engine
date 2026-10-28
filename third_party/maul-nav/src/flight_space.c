// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight volume's space: descents of the compact octrees
// to a voxel and over a face's slab, and the conservative voxel walk.

#include "flight_space.h"

#include "flight.h"
#include "flight_volume.h"
#include "invariant.h"

#include "maul-nav/flight.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// The deepest a slab's descent goes: eight children on each of the at most
// seven node levels of a 512-voxel cube.
#define STACK_DEPTH 64

// a divided by 2 to the shift, rounded down: for a negative a, the
// complement of the complement's quotient.
static int32_t FloorShift(int32_t a, int32_t shift)
{
    return a >= 0 ? a >> shift : -(int32_t)(~(uint32_t)a >> shift) - 1;
}

// The bits set in a mask of eight bits, counted in parallel.
static uint32_t Ones(uint32_t mask)
{
    mask = mask - (mask >> 1 & 0x55u);
    mask = (mask & 0x33u) + (mask >> 2 & 0x33u);
    return (mask + (mask >> 4)) & 0x0Fu;
}

mnavFlightCursor mnavMakeFlightCursor(const mnavFlightVolume* volume)
{
    int32_t side = volume->def.tileVoxels;
    int32_t shift = 0;
    while (1 << shift < side)
    {
        ++shift;
    }
    return (mnavFlightCursor){
        .volume = volume, .side = side, .shift = shift, .layers = volume->shape.cubeCount * side};
}

// The tile holding column (x, z), or NULL.
static const mnavFlightTile* TileOf(mnavFlightCursor* c, int32_t x, int32_t z)
{
    int32_t tileX = FloorShift(x, c->shift);
    int32_t tileZ = FloorShift(z, c->shift);
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

static bool Inside(const mnavFlightBlock* b, const int32_t at[3])
{
    return at[0] >= b->x && at[1] >= b->y && at[2] >= b->z && at[0] < b->x + b->size &&
           at[1] < b->y + b->size && at[2] < b->z + b->size;
}

// Moves block b to its child of side size holding voxel (x, y, z), and
// returns the child's octant.
static int32_t Halve(mnavFlightBlock* b, int32_t size, int32_t x, int32_t y, int32_t z)
{
    // A side of 16 voxels or more halves to the leaves' 4, which end a
    // descent.
    MNAV_ASSERT(size >= 4);
    int32_t o =
        (x >= b->x + size ? 1 : 0) | (y >= b->y + size ? 2 : 0) | (z >= b->z + size ? 4 : 0);
    b->x += (o & 1) != 0 ? size : 0;
    b->y += (o & 2) != 0 ? size : 0;
    b->z += (o & 4) != 0 ? size : 0;
    b->size = size;
    return o;
}

// Descends tile t to voxel (x, y, z) of its own frame: what holds it, and
// the block holding it; a leaf reached is kept in leaf and leafBits.
static int32_t Hold(const mnavFlightTile* t, int32_t x, int32_t y, int32_t z, mnavFlightBlock* b,
                    mnavFlightBlock* leaf, uint64_t* leafBits)
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
        int32_t o = Halve(b, size, x, y, z);
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
            *leaf = *b;
            *leafBits = t->leaves[child];
            *b = (mnavFlightBlock){x, y, z, 1};
            return (*leafBits >> bit & 1u) != 0 ? MNAV_SPACE_SOLID : MNAV_SPACE_OPEN;
        }
        node = &t->nodes[child];
    }
}

// Whether a voxel of the cursor's leaf is solid.
static bool LeafSolid(const mnavFlightCursor* c, const int32_t at[3])
{
    int32_t bit = (at[0] - c->leaf.x) + 4 * (at[1] - c->leaf.y) + 16 * (at[2] - c->leaf.z);
    return (c->leafBits >> bit & 1u) != 0;
}

int32_t mnavFlightHolder(mnavFlightCursor* cursor, int32_t x, int32_t y, int32_t z,
                         mnavFlightBlock* blockOut)
{
    const int32_t at[3] = {x, y, z};
    if (Inside(&cursor->leaf, at))
    {
        // The leaf's voxels are its blocks, as a descent finds them.
        if (blockOut != nullptr)
        {
            *blockOut = (mnavFlightBlock){x, y, z, 1};
        }
        return LeafSolid(cursor, at) ? MNAV_SPACE_SOLID : MNAV_SPACE_OPEN;
    }
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
    mnavFlightBlock leaf = {0, 0, 0, 0};
    uint64_t bits = 0;
    int32_t held = Hold(t, x - ox, y, z - oz, &b, &leaf, &bits);
    if (leaf.size != 0)
    {
        cursor->leaf = (mnavFlightBlock){leaf.x + ox, leaf.y, leaf.z + oz, leaf.size};
        cursor->leafBits = bits;
    }
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

static void Push(Frame* stack, int32_t* top, Frame f)
{
    MNAV_ASSERT(*top < STACK_DEPTH);
    stack[(*top)++] = f;
}

// The octant of a block of half side h at (x, y, z) that holds the whole
// region, or -1 when the region meets more than one.
static int32_t Holding(const Region* r, int32_t x, int32_t y, int32_t z, int32_t h)
{
    const int32_t mid[3] = {x + h, y + h, z + h};
    int32_t o = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        bool above = r->lo[k] >= mid[k];
        if (above != (r->hi[k] > mid[k]))
        {
            return -1;
        }
        o |= above ? 1 << k : 0;
    }
    return o;
}

// Descends cube c of tile t, which holds the region, to the deepest
// mixed node that still holds it whole, visiting the open block that
// does instead. Above it a search meets one child a node, so starting it
// there visits the same blocks in the same order. Returns whether a node
// is left to search, in f.
static bool Descend(const mnavFlightTile* t, int32_t c, const Region* r, Frame* f)
{
    int32_t side = t->side;
    if (t->roots[c] != MNAV_FLIGHT_MIXED)
    {
        if (t->roots[c] == MNAV_FLIGHT_EMPTY)
        {
            Visit(r, 0, c * side, 0, side);
        }
        return false;
    }
    *f = (Frame){(uint32_t)t->rootNodes[c], 0, c * side, 0, side};
    for (;;)
    {
        int32_t h = f->size / 2;
        int32_t o = Holding(r, f->x, f->y, f->z, h);
        if (o < 0)
        {
            return true;
        }
        const mnavFlightNode* node = &t->nodes[f->node];
        int32_t x = f->x + ((o & 1) != 0 ? h : 0);
        int32_t y = f->y + ((o & 2) != 0 ? h : 0);
        int32_t z = f->z + ((o & 4) != 0 ? h : 0);
        if ((node->mask >> (o + 8) & 1u) != 0)
        {
            return false;
        }
        if ((node->mask >> o & 1u) == 0)
        {
            Visit(r, x, y, z, h);
            return false;
        }
        if (h == 4)
        {
            VisitLeaf(r, t->leaves[Child(node, o)], x, y, z);
            return false;
        }
        *f = (Frame){Child(node, o), x, y, z, h};
    }
}

// Visits the open blocks of cube c of tile t within the region, which the
// cube holds.
static void Collect(const mnavFlightTile* t, int32_t c, const Region* r)
{
    Frame stack[STACK_DEPTH];
    int32_t top = 0;
    if (!Descend(t, c, r, &stack[0]))
    {
        return;
    }
    top = 1;
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
                Push(stack, &top, (Frame){Child(node, o), x, y, z, h});
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

// What holds the walk's voxel, keeping the open block that holds it.
static int32_t Look(mnavFlightCursor* cursor, Walk* w)
{
    if (Inside(&w->open, w->at))
    {
        return MNAV_SPACE_OPEN;
    }
    // Through a local, so that the walk's address never leaves the walk.
    mnavFlightBlock open;
    int32_t held = mnavFlightHolder(cursor, w->at[0], w->at[1], w->at[2], &open);
    w->open = open;
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

static double Min(double a, double b)
{
    return b < a ? b : a;
}

// The walk's next step: the fraction of the segment it is taken at, the
// axes crossed there (each due within 1e-9 of it), and the voxel entered.
// The fraction equals fmin's but for the sign of a zero, which neither
// the comparisons nor the crossing of an axis can tell.
static double Due(const Walk* w, uint32_t* tied, int32_t to[3])
{
    double t = Min(w->next[0], Min(w->next[1], w->next[2]));
    *tied = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        bool crossed = w->next[k] - t < 1e-9;
        *tied |= crossed ? 1u << k : 0u;
        to[k] = w->at[k] + (crossed ? w->step[k] : 0);
    }
    return t;
}

// Takes a step out of the open block, across the axes in tied to voxel
// to; returns what holds the first voxel it touches on the way that is
// not open, crossing an edge or a corner. Look tells what holds to.
static int32_t Advance(mnavFlightCursor* cursor, Walk* w, uint32_t tied, const int32_t to[3])
{
    int32_t held = (tied & (tied - 1u)) != 0 ? Corners(cursor, w, tied) : MNAV_SPACE_OPEN;
    for (int32_t k = 0; k < 3; ++k)
    {
        bool crossed = (tied >> k & 1u) != 0;
        w->at[k] = to[k];
        w->next[k] += crossed ? w->delta[k] : 0.0;
    }
    return held;
}

// Whether every voxel a step from at across the axes in tied touches,
// each reached by a part of the steps, is an open voxel of the cursor's
// leaf: at and to are in it, and so is every voxel between them.
static bool LeafClear(const mnavFlightCursor* c, const int32_t at[3], const int32_t step[3],
                      uint32_t tied, const int32_t to[3])
{
    if (!Inside(&c->leaf, at) || !Inside(&c->leaf, to))
    {
        return false;
    }
    for (uint32_t m = 1; m < 8; ++m)
    {
        if ((m & ~tied) != 0)
        {
            continue;
        }
        int32_t p[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            p[k] = at[k] + ((m >> k & 1u) != 0 ? step[k] : 0);
        }
        if (LeafSolid(c, p))
        {
            return false;
        }
    }
    return true;
}

// The crossings of axis k the walk makes before time before, from its
// next one, each added as a step adds it; Skip's check needs the first
// crossing at or past before, in at.
static int32_t Crossings(const Walk* w, int32_t k, double before, double* at)
{
    int32_t count = 0;
    double next = w->next[k];
    while (next < before)
    {
        next += w->delta[k];
        ++count;
    }
    *at = next;
    return count;
}

// The time of axis k's crossing number n from the walk's next, within
// far less than SKIP_BACK of the sum the steps add up to it.
static double CrossingAt(const Walk* w, int32_t k, int32_t n)
{
    return w->next[k] + (double)n * w->delta[k];
}

// The steps' margin: a step crosses every axis due within 1e-9 of it.
#define SKIP_BACK 1e-7
#define SKIP_GAP  1e-8

// Moves the walk at once across its open block, to where its steps would
// have taken it just before time B: every crossing due before B made, as
// the steps add it, and none after. B lies SKIP_BACK before the first
// crossing out of the block, before the last crossing the end's voxel
// needs when the block holds it, and before the segment's end, so no step
// before B leaves the block or stops the walk. With no crossing due from B
// to SKIP_GAP after it, every crossing before B is made by a step before
// it, and every one after by a step after it; else the walk is left as it
// is. An axis crossed every SKIP_GAP or less is never skipped.
static void Skip(Walk* w, const int32_t end[3])
{
    bool holdsEnd = Inside(&w->open, end);
    double out = 1.0;
    double reach = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (w->step[k] == 0)
        {
            continue;
        }
        if (w->delta[k] <= SKIP_GAP)
        {
            return;
        }
        // The crossings left in the block, the last of them out of it.
        int32_t lo = k == 0 ? w->open.x : (k == 1 ? w->open.y : w->open.z);
        int32_t room = w->step[k] > 0 ? lo + w->open.size - w->at[k] : w->at[k] - lo + 1;
        out = Min(out, CrossingAt(w, k, room - 1));
        int32_t need = (end[k] - w->at[k]) * w->step[k];
        if (holdsEnd && need > 0)
        {
            double last = CrossingAt(w, k, need - 1);
            reach = last > reach ? last : reach;
        }
    }
    double before = (holdsEnd ? Min(out, reach) : out) - SKIP_BACK;
    int32_t counts[3] = {0, 0, 0};
    double next[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        if (w->step[k] == 0)
        {
            next[k] = w->next[k];
            continue;
        }
        counts[k] = Crossings(w, k, before, &next[k]);
        if (next[k] < before + SKIP_GAP)
        {
            return;
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        w->at[k] += counts[k] * w->step[k];
        w->next[k] = next[k];
    }
}

// Moves the walk on while every voxel it touches is known open: in its
// open block, or an open voxel of the cursor's leaf, which becomes the
// block. Returns false where the walk ends open, at the end's voxel or
// past the segment's end, and true when its next step needs Advance and
// Look.
static bool Within(const mnavFlightCursor* cursor, Walk* w, const int32_t end[3], uint32_t* tied,
                   int32_t to[3])
{
    if (w->open.size >= 4)
    {
        Skip(w, end);
    }
    while (w->at[0] != end[0] || w->at[1] != end[1] || w->at[2] != end[2])
    {
        if (Due(w, tied, to) > 1.0)
        {
            return false;
        }
        // A voxel short of the end's lies a step away along an axis the
        // segment moves on, whose crossing is the one due; without it the
        // walk would not move.
        MNAV_ASSERT(*tied != 0);
        if (!Inside(&w->open, to))
        {
            if (!LeafClear(cursor, w->at, w->step, *tied, to))
            {
                return true;
            }
            w->open = (mnavFlightBlock){to[0], to[1], to[2], 1};
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            w->next[k] += (*tied >> k & 1u) != 0 ? w->delta[k] : 0.0;
            w->at[k] = to[k];
        }
    }
    return false;
}

int32_t mnavFlightWalk(mnavFlightCursor* cursor, const double a[3], const double b[3], double* tOut)
{
    Walk w = StartWalk(a, b);
    const int32_t end[3] = {(int32_t)floor(b[0]), (int32_t)floor(b[1]), (int32_t)floor(b[2])};
    // The crossings due before the last step out of a block, whose
    // fraction a refused walk gives.
    double before[3] = {0.0, 0.0, 0.0};
    uint32_t tied = 0;
    int32_t to[3];
    int32_t held = Look(cursor, &w);
    while (held == MNAV_SPACE_OPEN && Within(cursor, &w, end, &tied, to))
    {
        memcpy(before, w.next, sizeof(before));
        held = Advance(cursor, &w, tied, to);
        held = held == MNAV_SPACE_OPEN ? Look(cursor, &w) : held;
    }
    if (held != MNAV_SPACE_OPEN && tOut != nullptr)
    {
        *tOut = fmin(before[0], fmin(before[1], before[2]));
    }
    return held;
}
