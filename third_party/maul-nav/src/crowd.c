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
// their own, nearest first, and the search stops at a ring that cannot
// hold a nearer one, so the cells' size changes the work, never the
// neighbours found. Small cells cost a probe each, large ones agents
// looked at and passed over; on the ground the best size holds about
// three agents in each cell probed (the benchmark's doorway runs fastest
// with two cells per range, its crowd with four, the doorway scenario
// with six), so a call whose probes held under two or over six agents
// each sizes the next call's cells for three, and one between keeps
// them; at most six per range, as eight took 10% more instructions than
// six in the doorway scenario's jam. In space two, as a shell of cubes
// grows with the cube of its cells: three take 14% fewer instructions in
// the benchmark's space step but ran 0.4% to 1.5% slower on a quiet core,
// and cubes sized from the last call made a dense swarm 4% faster but the
// benchmark's gap 3% and a sparse formation 1% slower.
#define GROUND_FIRST  4
#define GROUND_FEWEST 2
#define GROUND_MOST   6
#define HELD_LOW      2.0
#define HELD_AIM      3.0
#define HELD_HIGH     6.0

static int64_t Divisions(const mnavCrowd* crowd)
{
    return crowd->space ? 2 : crowd->divisions;
}

void mnavBeginCrowd(mnavCrowd* crowd, bool space)
{
    crowd->space = space;
    crowd->divisions = crowd->divisions == 0 ? GROUND_FIRST : crowd->divisions;
    crowd->probed = 0;
    crowd->scanned = 0;
}

void mnavEndCrowd(mnavCrowd* crowd)
{
    if (crowd->space || crowd->probed == 0)
    {
        return;
    }
    double held = (double)crowd->scanned / (double)crowd->probed;
    if (held >= HELD_LOW && held <= HELD_HIGH)
    {
        return;
    }
    // Agents per probe fall with the square of the divisions: the most
    // divisions whose probes would still hold HELD_AIM.
    double now = (double)crowd->divisions;
    int32_t next = GROUND_FEWEST;
    while (next < GROUND_MOST &&
           held * now * now >= HELD_AIM * (double)(next + 1) * (double)(next + 1))
    {
        next += 1;
    }
    crowd->divisions = next;
}

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
    double size = crowd->range / (double)Divisions(crowd);
    int64_t z = crowd->space ? CellOf(position.z, size) : 0;
    return (mnavCrowdKey){
        CellOf(position.x, size), CellOf(position.y, size), z, id, index, position, 0};
}

static bool SameColumn(const mnavCrowdKey* a, const mnavCrowdKey* b)
{
    return a->x == b->x && a->y == b->y;
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

static uint32_t ColumnHash(int64_t x, int64_t y)
{
    uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15ull ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Full;
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
    // Each occupied column's run of keys, its cells along z in order,
    // probed linearly from its hash; on the ground a column is a cell.
    mnavCrowdRun* run = nullptr;
    for (int32_t k = 0; k < count; ++k)
    {
        const mnavCrowdKey* key = &crowd->keys[k];
        if (k > 0 && SameColumn(key, &crowd->keys[k - 1]))
        {
            run->end = k + 1;
            continue;
        }
        uint32_t slot = ColumnHash(key->x, key->y) & crowd->tableMask;
        while (crowd->table[slot].first >= 0)
        {
            slot = (slot + 1u) & crowd->tableMask;
        }
        run = &crowd->table[slot];
        *run = (mnavCrowdRun){k, k + 1};
    }
}

// The keys of a column, an empty run when no agent is in it.
static mnavCrowdRun RunIn(const mnavCrowd* crowd, int64_t x, int64_t y)
{
    uint32_t slot = ColumnHash(x, y) & crowd->tableMask;
    for (mnavCrowdRun run = crowd->table[slot]; run.first >= 0; run = crowd->table[slot])
    {
        const mnavCrowdKey* key = &crowd->keys[run.first];
        if (key->x == x && key->y == y)
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
    uint32_t ignores;
    int32_t found;
} Search;

// Keeps the agents within range of a column's cells from z0 to z1, or of
// those two cells alone when ends is set.
static void Scan(Search* s, int64_t x, int64_t y, int64_t z0, int64_t z1, bool ends)
{
    mnavCrowd* crowd = s->crowd;
    mnavCrowdRun run = RunIn(crowd, x, y);
    crowd->probed += 1;
    crowd->scanned += run.end - run.first;
    for (int32_t k = run.first; k < run.end && crowd->keys[k].z <= z1; ++k)
    {
        const mnavCrowdKey* key = &crowd->keys[k];
        if (key->z < z0 || (ends && key->z != z0 && key->z != z1))
        {
            continue;
        }
        double dx = key->position.x - s->position.x;
        double dy = key->position.y - s->position.y;
        double dz = key->position.z - s->position.z;
        double distance = dx * dx + dy * dy + dz * dz;
        if (key->index != s->index && distance < s->rangeSq && (key->layers & s->ignores) == 0)
        {
            s->found = Insert(crowd->neighbors, s->found, crowd->limit,
                              (mnavCrowdNeighbor){distance, key->id, key->index});
        }
    }
}

// Scans the cells r out from c: a ring of squares on the ground plane, a
// shell of cubes in space, a column at a time. A column on the shell's
// side is in it from end to end; one inside, only at its two ends.
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
            Scan(s, x, y, c->z - rz, c->z + rz, !side);
        }
    }
}

// The rings or shells from the agent's own cell out. A cell r out lies
// at least r - 1 cells away, a little less for the cells' rounding; once
// the list is full, a ring nearer than that to nothing kept holds no
// agent it would keep (ties at the worst distance are still looked at),
// and the search stops. The neighbours are those a search of every cell
// in range finds.
int32_t mnavCrowdNeighbors(mnavCrowd* crowd, mnavPos3 position, int32_t index, uint32_t ignores)
{
    double range = crowd->range;
    int64_t divisions = Divisions(crowd);
    double size = range / (double)divisions;
    mnavCrowdKey center = mnavCrowdKeyOf(crowd, position, 0, index);
    Search s = {crowd, position, index, range * range, ignores, 0};
    for (int64_t r = 0; r <= divisions + 1; ++r)
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
