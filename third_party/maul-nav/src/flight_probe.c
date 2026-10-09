// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flier's raycast and nearest open point (mnav-0015).

#include "flight.h"
#include "flight_frame.h"
#include "flight_space.h"
#include "flight_volume.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"

#include <math.h>
#include <stdint.h>

// How far inside its block a nearest point lies, in voxels, so that the
// voxel holding it is the open one.
#define INSET (1.0 / 1024.0)

// See flight_space.c: eight children on each of at most seven levels.
#define STACK_DEPTH 64

mnavResult mnavFlightRaycast(const mnavFlightVolume* volume, mnavPos3 from, mnavPos3 to,
                             mnavFlightHit* hitOut)
{
    if (volume == nullptr || hitOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavFlightFrame frame = mnavMakeFlightFrame(volume);
    double a[3];
    double b[3];
    int32_t at[3];
    int32_t to3[3];
    if (!mnavFlightToVoxels(&frame, from, a, at) || !mnavFlightToVoxels(&frame, to, b, to3))
    {
        return mnav_errorRange;
    }
    mnavFlightCursor cursor = mnavMakeFlightCursor(volume);
    double t = 1.0;
    int32_t held = mnavFlightWalk(&cursor, a, b, &t);
    t = held == MNAV_SPACE_OPEN ? 1.0 : t;
    hitOut->stop = held == MNAV_SPACE_OPEN    ? mnav_flightClear
                   : held == MNAV_SPACE_SOLID ? mnav_flightBlocked
                                              : mnav_flightNotLoaded;
    hitOut->t = t;
    hitOut->point = (mnavPos3){from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
                               from.z + (to.z - from.z) * t};
    return mnav_success;
}

// The nearest point search: the point, in voxels, the best distance so
// far (the radius before any), and the point found.
typedef struct Nearest
{
    double p[3];
    double best;
    double found[3];
    bool any;
} Nearest;

// The distance from the point to a box, 0 inside it.
static double BoxDistance(const Nearest* n, double x, double y, double z, double size)
{
    const double lo[3] = {x, y, z};
    double sum = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double d = n->p[k] < lo[k] ? lo[k] - n->p[k] : n->p[k] - (lo[k] + size);
        sum += d > 0.0 ? d * d : 0.0;
    }
    return sqrt(sum);
}

// Takes an open block's nearest point, inset, if it is the nearest yet.
static void Consider(Nearest* n, int32_t x, int32_t y, int32_t z, int32_t size)
{
    const double lo[3] = {x, y, z};
    double q[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        double low = lo[k] + INSET;
        double high = lo[k] + (double)size - INSET;
        q[k] = n->p[k] < low ? low : (n->p[k] > high ? high : n->p[k]);
    }
    double dx = q[0] - n->p[0];
    double dy = q[1] - n->p[1];
    double dz = q[2] - n->p[2];
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d <= n->best)
    {
        n->best = d;
        n->found[0] = q[0];
        n->found[1] = q[1];
        n->found[2] = q[2];
        n->any = true;
    }
}

static void ConsiderLeaf(Nearest* n, uint64_t leaf, int32_t x, int32_t y, int32_t z)
{
    for (int32_t k = 0; k < 64; ++k)
    {
        if ((leaf >> k & 1u) == 0)
        {
            Consider(n, x + (k & 3), y + (k >> 2 & 3), z + (k >> 4), 1);
        }
    }
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

typedef struct Frame
{
    uint32_t node;
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t size;
} Frame;

// Searches a mixed cube's octree, whose root node is root, at (x, y, z)
// in the volume's frame, skipping blocks no nearer than the best.
static void SearchCube(Nearest* n, const mnavFlightTile* t, uint32_t root, int32_t x, int32_t y,
                       int32_t z)
{
    Frame stack[STACK_DEPTH];
    int32_t top = 0;
    stack[top++] = (Frame){root, x, y, z, t->side};
    while (top > 0)
    {
        Frame f = stack[--top];
        const mnavFlightNode* node = &t->nodes[f.node];
        int32_t h = f.size / 2;
        for (int32_t o = 0; o < 8; ++o)
        {
            int32_t cx = f.x + ((o & 1) != 0 ? h : 0);
            int32_t cy = f.y + ((o & 2) != 0 ? h : 0);
            int32_t cz = f.z + ((o & 4) != 0 ? h : 0);
            if ((node->mask >> (o + 8) & 1u) != 0 || BoxDistance(n, cx, cy, cz, h) > n->best)
            {
                continue;
            }
            uint32_t child = node->firstChild + Ones(node->mask & ((1u << o) - 1u));
            if ((node->mask >> o & 1u) == 0)
            {
                Consider(n, cx, cy, cz, h);
            }
            else if (h == 4)
            {
                ConsiderLeaf(n, t->leaves[child], cx, cy, cz);
            }
            else
            {
                stack[top++] = (Frame){child, cx, cy, cz, h};
            }
        }
    }
}

// Searches every cube of a committed tile near enough.
static void SearchTile(Nearest* n, const mnavFlightEntry* e)
{
    const mnavFlightTile* t = &e->tile;
    int32_t ox = e->x * t->side;
    int32_t oz = e->z * t->side;
    for (int32_t c = 0; c < t->cubeCount; ++c)
    {
        int32_t y = c * t->side;
        if (t->roots[c] == MNAV_FLIGHT_SOLID || BoxDistance(n, ox, y, oz, t->side) > n->best)
        {
            continue;
        }
        if (t->roots[c] == MNAV_FLIGHT_EMPTY)
        {
            Consider(n, ox, y, oz, t->side);
        }
        else
        {
            SearchCube(n, t, (uint32_t)t->rootNodes[c], ox, y, oz);
        }
    }
}

mnavResult mnavFindNearestFlightPoint(const mnavFlightVolume* volume, mnavPos3 point, float radius,
                                      mnavPos3* nearestOut, bool* foundOut)
{
    if (volume == nullptr || nearestOut == nullptr || foundOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *foundOut = false;
    *nearestOut = point;
    mnavFlightFrame frame = mnavMakeFlightFrame(volume);
    Nearest n = {{0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0}, false};
    int32_t at[3];
    if (!mnavFlightToVoxels(&frame, point, n.p, at) || !(radius >= 0.0f) || !isfinite(radius))
    {
        return mnav_errorRange;
    }
    mnavFlightCursor cursor = mnavMakeFlightCursor(volume);
    if (mnavFlightHolder(&cursor, at[0], at[1], at[2], nullptr) == MNAV_SPACE_OPEN)
    {
        *foundOut = true;
        return mnav_success;
    }
    n.best = (double)radius / frame.voxel;
    for (int32_t i = 0; i < volume->tileCount; ++i)
    {
        SearchTile(&n, &volume->tiles[i]);
    }
    if (n.any)
    {
        *foundOut = true;
        *nearestOut = mnavFlightToWorld(&frame, n.found);
    }
    return mnav_success;
}
