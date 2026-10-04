// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Raycasts along the navmesh (mnav-0005).

#include "navmesh.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>

// A ray on the ground: from start along way, and the polygon it is in.
typedef struct Walk
{
    const mnavNavmesh* navmesh;
    mnavPos3 start;
    double wayX;
    double wayZ;
    const mnavQueryFilter* filter;
    int32_t slot;
    int32_t polygon;
} Walk;

// Where the ray, already at t, leaves through edge (a, b): the parameter at
// which it crosses the edge's line outward, or INFINITY when it never does.
static double Leaves(const Walk* w, mnavPos3 a, mnavPos3 b)
{
    double ex = b.x - a.x;
    double ez = b.z - a.z;
    double outward = ex * w->wayZ - ez * w->wayX;
    if (outward <= 0.0)
    {
        return (double)INFINITY;
    }
    double side = ex * (w->start.z - a.z) - (w->start.x - a.x) * ez;
    return -side / outward;
}

// The polygon the ray enters through edge j at parameter t: true with the
// walk moved there, false at a wall; notLoaded is set at a side with no
// tile beyond.
static bool Follow(Walk* w, int32_t j, double t, bool* notLoaded)
{
    const mnavSlot* slot = &w->navmesh->slots[w->slot];
    const mnavTile* tile = slot->tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[w->polygon];
    if (polygon->neighbors[j] != MNAV_NO_INDEX)
    {
        uint16_t next = polygon->neighbors[j];
        if (!mnavIncludes(w->filter, tile->mesh.polygons[next].area))
        {
            return false;
        }
        w->polygon = next;
        return true;
    }
    int32_t side = polygon->sides[j];
    if (side == 0)
    {
        return false;
    }
    int32_t x = slot->x;
    int32_t z = slot->z;
    int32_t facing = 0;
    int32_t across = -1;
    mnavAcross(side, &x, &z, &facing);
    if (mnavTileAt(w->navmesh, x, z, &across) == nullptr)
    {
        *notLoaded = true;
        return false;
    }
    // The exit point along the side, in the tile's cells.
    mnavFrame f = mnavFrameOf(w->navmesh, slot->x, slot->z);
    bool alongZ = side == 1 || side == 3;
    double u = alongZ ? (w->start.z + t * w->wayZ - f.z0) / f.cell
                      : (w->start.x + t * w->wayX - f.x0) / f.cell;
    for (int32_t l = tile->firstLink[w->polygon]; l < tile->firstLink[w->polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        const mnavTile* target = w->navmesh->slots[link->target.slot - 1].tile;
        bool included = mnavIncludes(w->filter, target->mesh.polygons[link->target.polygon].area);
        if (link->edge == j && u >= link->low && u <= link->high && included)
        {
            w->slot = (int32_t)link->target.slot - 1;
            w->polygon = (int32_t)link->target.polygon;
            return true;
        }
    }
    return false;
}

// The first parameter at which the ray leaves its polygon, not before t.
static double Exit(const Walk* w, double t, mnavPos3* corners, int32_t* count)
{
    const mnavSlot* slot = &w->navmesh->slots[w->slot];
    const mnavPolyMesh* mesh = &slot->tile->mesh;
    const mnavPolygon* polygon = &mesh->polygons[w->polygon];
    mnavFrame f = mnavFrameOf(w->navmesh, slot->x, slot->z);
    *count = polygon->count;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        corners[k] = mnavVertexWorld(&f, &mesh->vertices[polygon->vertices[k]]);
    }
    double best = (double)INFINITY;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        double leaves = Leaves(w, corners[k], corners[(k + 1) % polygon->count]);
        best = leaves < best ? leaves : best;
    }
    return best > t ? best : t;
}

static void Record(mnavQuery* query, const Walk* w, int32_t* count)
{
    query->corridor[(*count)++] = (mnavPolygonId){
        (uint32_t)w->slot + 1, w->navmesh->slots[w->slot].generation, (uint32_t)w->polygon};
}

// Stops the ray at a wall: edge (a, b)'s normal, pointing inside.
static void Wall(mnavRay* ray, mnavPos3 a, mnavPos3 b)
{
    double ex = b.x - a.x;
    double ez = b.z - a.z;
    double length = sqrt(ex * ex + ez * ez);
    ray->end = mnav_rayWall;
    ray->normalX = ez / length;
    ray->normalZ = -ex / length;
}

mnavResult mnavRaycast(mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                       mnavPolygonId startPolygon, mnavPos3 start, mnavPos3 end, mnavRay* rayOut)
{
    if (query == nullptr || navmesh == nullptr || rayOut == nullptr || !isfinite(start.x) ||
        !isfinite(start.y) || !isfinite(start.z) || !isfinite(end.x) || !isfinite(end.z))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavCheckPolygon(navmesh, startPolygon);
    const mnavQueryFilter* usable = nullptr;
    result = result == mnav_success ? mnavCheckFilter(filter, &usable) : result;
    if (result != mnav_success)
    {
        return result;
    }
    Walk w = {navmesh,
              start,
              end.x - start.x,
              end.z - start.z,
              usable,
              (int32_t)startPolygon.slot - 1,
              (int32_t)startPolygon.polygon};
    *rayOut = (mnavRay){mnav_rayReached, 1.0, 0.0, 0.0, query->corridor, 0};
    double t = 0.0;
    int32_t count = 0;
    Record(query, &w, &count);
    for (;;)
    {
        mnavPos3 corners[MNAV_POLYGON_VERTICES];
        int32_t sides = 0;
        double exit = Exit(&w, t, corners, &sides);
        if (exit >= 1.0)
        {
            rayOut->t = 1.0;
            break;
        }
        t = exit;
        rayOut->t = t;
        if (count == query->limits.nodes)
        {
            rayOut->end = mnav_rayOutOfNodes;
            break;
        }
        // Every edge the ray leaves through at this parameter, the ones
        // that lead on first, the lowest-numbered first.
        bool moved = false;
        bool notLoaded = false;
        int32_t wall = -1;
        for (int32_t k = 0; k < sides && !moved; ++k)
        {
            if (Leaves(&w, corners[k], corners[(k + 1) % sides]) <= t)
            {
                wall = wall < 0 ? k : wall;
                moved = Follow(&w, k, t, &notLoaded);
            }
        }
        if (moved)
        {
            Record(query, &w, &count);
            continue;
        }
        if (notLoaded)
        {
            rayOut->end = mnav_rayNotLoaded;
        }
        else
        {
            Wall(rayOut, corners[wall], corners[(wall + 1) % sides]);
        }
        break;
    }
    rayOut->polygonCount = count;
    return mnav_success;
}
