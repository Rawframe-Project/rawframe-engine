// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The neighbour grid of avoidance (mnav-0006): keys sorted by cell and
// id with a stable merge, cells found by a linear-probed hash, and
// neighbours by rings or shells of cells, nearest first.

#include "crowd.h"

#include "maul-nav/base.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// The largest cell index, far inside 64-bit integers so that a cell and
// its neighbours stay exact.
#define CELL_LIMIT 0x1p52

// Cells per neighbour range: agents are found in rings of cells round
// their own, nearest first.
#define CELL_DIVISIONS 2

int32_t mnavCrowdTableSize(int32_t agents)
{
    int32_t size = 2;
    while (size < 2 * agents)
    {
        size *= 2;
    }
    return size;
}

// The cell holding v; saturated, so that any distance gives a cell.
// Saturation keeps cells in order and neighbours within one of each
// other, which is all the search needs.
static int64_t CellOf(double v, double size)
{
    double cell = floor(v / size);
    return cell < -CELL_LIMIT ? (int64_t)-CELL_LIMIT
                              : (cell > CELL_LIMIT ? (int64_t)CELL_LIMIT : (int64_t)cell);
}

mnavCrowdKey mnavCrowdKeyOf(const mnavCrowd* crowd, mnavPos3 position, uint64_t id, int32_t index)
{
    double size = crowd->range / CELL_DIVISIONS;
    int64_t z = crowd->space ? CellOf(position.z, size) : 0;
    return (mnavCrowdKey){
        CellOf(position.x, size), CellOf(position.y, size), z, id, index, position};
}

static bool SameCell(const mnavCrowdKey* a, const mnavCrowdKey* b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z;
}

static bool KeyBefore(const mnavCrowdKey* a, const mnavCrowdKey* b)
{
    if (a->x != b->x)
    {
        return a->x < b->x;
    }
    if (a->y != b->y)
    {
        return a->y < b->y;
    }
    if (a->z != b->z)
    {
        return a->z < b->z;
    }
    return a->id != b->id ? a->id < b->id : a->index < b->index;
}

// Sorts the keys, a stable bottom-up merge through the scratch.
static void SortKeys(mnavCrowdKey* keys, mnavCrowdKey* scratch, int32_t count)
{
    mnavCrowdKey* from = keys;
    mnavCrowdKey* to = scratch;
    for (int32_t width = 1; width < count; width *= 2)
    {
        for (int32_t start = 0; start < count; start += 2 * width)
        {
            int32_t middle = start + width < count ? start + width : count;
            int32_t end = start + 2 * width < count ? start + 2 * width : count;
            int32_t i = start;
            int32_t j = middle;
            for (int32_t k = start; k < end; ++k)
            {
                bool left = i < middle && (j >= end || !KeyBefore(&from[j], &from[i]));
                to[k] = left ? from[i++] : from[j++];
            }
        }
        mnavCrowdKey* swap = from;
        from = to;
        to = swap;
    }
    for (int32_t k = 0; from != keys && k < count; ++k)
    {
        keys[k] = from[k];
    }
}

static uint32_t CellHash(int64_t x, int64_t y, int64_t z)
{
    uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15ull ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Full ^
                 (uint64_t)z * 0x165667B19E3779F9ull;
    return (uint32_t)(h ^ (h >> 32));
}

void mnavSortCrowd(mnavCrowd* crowd, int32_t count)
{
    SortKeys(crowd->keys, crowd->scratch, count);
    int32_t size = mnavCrowdTableSize(count);
    crowd->tableMask = (uint32_t)size - 1u;
    for (int32_t s = 0; s < size; ++s)
    {
        crowd->table[s] = (mnavCrowdRun){-1, -1};
    }
    // Each occupied cell's run of keys, probed linearly from its hash.
    mnavCrowdRun* run = nullptr;
    for (int32_t k = 0; k < count; ++k)
    {
        const mnavCrowdKey* key = &crowd->keys[k];
        if (k > 0 && SameCell(key, &crowd->keys[k - 1]))
        {
            run->end = k + 1;
            continue;
        }
        uint32_t slot = CellHash(key->x, key->y, key->z) & crowd->tableMask;
        while (crowd->table[slot].first >= 0)
        {
            slot = (slot + 1u) & crowd->tableMask;
        }
        run = &crowd->table[slot];
        *run = (mnavCrowdRun){k, k + 1};
    }
}

// The keys of a cell, an empty run when no agent is in it.
static mnavCrowdRun RunIn(const mnavCrowd* crowd, int64_t x, int64_t y, int64_t z)
{
    uint32_t slot = CellHash(x, y, z) & crowd->tableMask;
    for (mnavCrowdRun run = crowd->table[slot]; run.first >= 0; run = crowd->table[slot])
    {
        const mnavCrowdKey* key = &crowd->keys[run.first];
        if (key->x == x && key->y == y && key->z == z)
        {
            return run;
        }
        slot = (slot + 1u) & crowd->tableMask;
    }
    return (mnavCrowdRun){0, 0};
}

static bool NeighborBefore(const mnavCrowdNeighbor* a, const mnavCrowdNeighbor* b)
{
    if (a->distance != b->distance)
    {
        return a->distance < b->distance;
    }
    return a->id != b->id ? a->id < b->id : a->index < b->index;
}

// Keeps a candidate among the nearest, up to the limit; returns the count.
static int32_t Insert(mnavCrowdNeighbor* list, int32_t count, int32_t limit,
                      mnavCrowdNeighbor candidate)
{
    if (count == limit && !NeighborBefore(&candidate, &list[count - 1]))
    {
        return count;
    }
    int32_t i = count < limit ? count++ : count - 1;
    while (i > 0 && NeighborBefore(&candidate, &list[i - 1]))
    {
        list[i] = list[i - 1];
        i -= 1;
    }
    list[i] = candidate;
    return count;
}

// The search for one agent's neighbours.
typedef struct Search
{
    mnavCrowd* crowd;
    mnavPos3 position;
    int32_t index;
    double rangeSq;
    int32_t found;
} Search;

// Keeps the agents of a cell within range.
static void Scan(Search* s, int64_t x, int64_t y, int64_t z)
{
    mnavCrowd* crowd = s->crowd;
    mnavCrowdRun run = RunIn(crowd, x, y, z);
    for (int32_t k = run.first; k < run.end; ++k)
    {
        const mnavCrowdKey* key = &crowd->keys[k];
        double dx = key->position.x - s->position.x;
        double dy = key->position.y - s->position.y;
        double dz = key->position.z - s->position.z;
        double distance = dx * dx + dy * dy + dz * dz;
        if (key->index != s->index && distance < s->rangeSq)
        {
            s->found = Insert(crowd->neighbors, s->found, crowd->limit,
                              (mnavCrowdNeighbor){distance, key->id, key->index});
        }
    }
}

// Scans the cells r out from c: a ring of squares on the ground plane, a
// shell of cubes in space.
static void Ring(Search* s, const mnavCrowdKey* c, int64_t r)
{
    int64_t rz = s->crowd->space ? r : 0;
    for (int64_t x = c->x - r; x <= c->x + r; ++x)
    {
        bool sideX = x == c->x - r || x == c->x + r;
        int64_t stepY = sideX || rz > 0 || r == 0 ? 1 : 2 * r;
        for (int64_t y = c->y - r; y <= c->y + r; y += stepY)
        {
            bool side = sideX || y == c->y - r || y == c->y + r;
            for (int64_t z = c->z - rz; z <= c->z + rz; z += side || rz == 0 ? 1 : 2 * rz)
            {
                Scan(s, x, y, z);
            }
        }
    }
}

// The rings or shells from the agent's own cell out. A cell r out lies
// at least r - 1 cells away, a little less for the cells' rounding; once
// the list is full, a ring nearer than that to nothing kept holds no
// agent it would keep (ties at the worst distance are still looked at),
// and the search stops. The neighbours are those a search of every cell
// in range finds.
int32_t mnavCrowdNeighbors(mnavCrowd* crowd, mnavPos3 position, int32_t index)
{
    double range = crowd->range;
    double size = range / CELL_DIVISIONS;
    mnavCrowdKey center = mnavCrowdKeyOf(crowd, position, 0, index);
    Search s = {crowd, position, index, range * range, 0};
    for (int64_t r = 0; r <= CELL_DIVISIONS + 1; ++r)
    {
        double near = r > 1 ? (double)(r - 1) * size * (1.0 - 0x1p-20) : 0.0;
        if (near >= range ||
            (s.found == crowd->limit && near * near > crowd->neighbors[s.found - 1].distance))
        {
            break;
        }
        Ring(&s, &center, r);
    }
    return s.found;
}
