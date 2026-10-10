// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Moving along the surface (mnav-0005): a breadth-first walk toward a
// wanted point, stopped by walls.

#include "navmesh.h"
#include "nearest.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// A point on the ground, in world meters.
typedef struct Ground
{
    double x;
    double z;
} Ground;

// The walk: the wanted point, the circle round the move, and the nearest
// wall point found so far.
typedef struct Mover
{
    mnavQuery* query;
    const mnavNavmesh* navmesh;
    const mnavQueryFilter* filter;
    Ground wanted;
    Ground center;
    double radius2;
    Ground best;
    double bestDistance;
    int32_t bestNode;
    bool bestUnloaded;
    bool outOfNodes;
    int32_t tail;
} Mover;

static double Distance2(Ground a, Ground b)
{
    return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z);
}

// The point of segment ab nearest p.
static Ground Nearest(Ground a, Ground b, Ground p)
{
    double dx = b.x - a.x;
    double dz = b.z - a.z;
    double length2 = dx * dx + dz * dz;
    double t = length2 > 0.0 ? ((p.x - a.x) * dx + (p.z - a.z) * dz) / length2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return (Ground){a.x + t * dx, a.z + t * dz};
}

// Offers a wall point; the nearest to the wanted point wins, the first met
// on ties.
static void Wall(Mover* m, int32_t n, Ground p, bool unloaded)
{
    double d = Distance2(p, m->wanted);
    if (d < m->bestDistance)
    {
        m->best = p;
        m->bestDistance = d;
        m->bestNode = n;
        m->bestUnloaded = unloaded;
    }
}

// Visits the polygon across a stretch ab of node n's polygon's edge, when
// the stretch meets the circle round the move.
static void Visit(Mover* m, int32_t n, int32_t slot, int32_t polygon, Ground a, Ground b)
{
    if (Distance2(Nearest(a, b, m->center), m->center) > m->radius2)
    {
        return;
    }
    mnavQuery* query = m->query;
    uint32_t cell = mnavFindNode(query, slot, polygon, 0, 0);
    if (query->table[cell] != MNAV_NO_NODE)
    {
        return;
    }
    if (query->nodeCount == query->limits.nodes)
    {
        m->outOfNodes = true;
        return;
    }
    int32_t next = query->nodeCount++;
    query->nodes[next] = (mnavSearchNode){0};
    query->nodes[next].slot = slot;
    query->nodes[next].polygon = polygon;
    query->nodes[next].parent = n;
    query->table[cell] = next;
    query->indexHeap[m->tail++] = next;
}

// An edge of the polygon being walked: its world ends, and its ends along
// the tile side it lies on, in cells.
typedef struct Edge
{
    Ground a;
    Ground b;
    double au;
    double bu;
} Edge;

static Ground At(const Edge* e, double u)
{
    double t = (u - e->au) / (e->bu - e->au);
    return (Ground){e->a.x + t * (e->b.x - e->a.x), e->a.z + t * (e->b.z - e->a.z)};
}

// Whether a link of edge j leads to a polygon the filter includes.
static bool Open(const Mover* m, const mnavLink* link, int32_t j)
{
    const mnavTile* across = m->navmesh->slots[link->target.slot - 1].tile;
    return link->edge == j &&
           mnavIncludes(m->filter, across->mesh.polygons[link->target.polygon].area);
}

// Whether u lies on a wall of edge j: in the closure of the part of the
// side no open link covers.
static bool OnWall(const Mover* m, const mnavTile* tile, int32_t polygon, int32_t j, const Edge* e,
                   double u)
{
    bool below = u <= (e->au < e->bu ? e->au : e->bu);
    bool above = u >= (e->au < e->bu ? e->bu : e->au);
    for (int32_t l = tile->firstLink[polygon]; l < tile->firstLink[polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        if (Open(m, link, j))
        {
            below = below || (link->low < u && u <= link->high);
            above = above || (link->low <= u && u < link->high);
        }
    }
    // Covered on both sides, u is open; a point inside a link is.
    return !(below && above);
}

// A tile-side edge: its open links are visited; the rest of it is wall,
// offered at the point nearest the wanted one, which is its projection
// when that lies on a wall, else an end of a covered stretch.
static void Side(Mover* m, int32_t n, const mnavTile* tile, int32_t polygon, int32_t j,
                 const Edge* e)
{
    for (int32_t l = tile->firstLink[polygon]; l < tile->firstLink[polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        if (Open(m, link, j))
        {
            Visit(m, n, (int32_t)link->target.slot - 1, (int32_t)link->target.polygon,
                  At(e, link->low), At(e, link->high));
        }
    }
    Ground q = Nearest(e->a, e->b, m->wanted);
    double along = fabs(e->b.x - e->a.x) > fabs(e->b.z - e->a.z)
                       ? (q.x - e->a.x) / (e->b.x - e->a.x)
                       : (q.z - e->a.z) / (e->b.z - e->a.z);
    if (OnWall(m, tile, polygon, j, e, e->au + along * (e->bu - e->au)))
    {
        Wall(m, n, q, false);
    }
    for (int32_t l = tile->firstLink[polygon]; l < tile->firstLink[polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        for (int32_t k = 0; k < 2 && Open(m, link, j); ++k)
        {
            double u = k == 0 ? link->low : link->high;
            if (OnWall(m, tile, polygon, j, e, u))
            {
                Wall(m, n, At(e, u), false);
            }
        }
    }
}

// Whether the wanted point lies in a polygon, its edges included.
static bool Holds(const Ground* corners, int32_t count, Ground p)
{
    for (int32_t k = 0; k < count; ++k)
    {
        Ground a = corners[k];
        Ground b = corners[(k + 1) % count];
        if ((b.x - a.x) * (p.z - a.z) - (p.x - a.x) * (b.z - a.z) > 0.0)
        {
            return false;
        }
    }
    return true;
}

// Walks node n's polygon: true when it holds the wanted point; otherwise
// visits its open neighbours and offers its walls.
static bool Walk(Mover* m, int32_t n)
{
    const mnavSearchNode* node = &m->query->nodes[n];
    const mnavSlot* slot = &m->navmesh->slots[node->slot];
    const mnavTile* tile = slot->tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    mnavFrame f = mnavFrameOf(m->navmesh, slot->x, slot->z);
    Ground corners[MNAV_POLYGON_VERTICES];
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        mnavPos3 p = mnavVertexWorld(&f, &tile->mesh.vertices[polygon->vertices[k]]);
        corners[k] = (Ground){p.x, p.z};
    }
    if (Holds(corners, polygon->count, m->wanted))
    {
        return true;
    }
    for (int32_t j = 0; j < polygon->count; ++j)
    {
        Ground a = corners[j];
        Ground b = corners[(j + 1) % polygon->count];
        int32_t next = polygon->neighbors[j];
        int32_t side = polygon->sides[j];
        if (next != MNAV_NO_INDEX && mnavIncludes(m->filter, tile->mesh.polygons[next].area))
        {
            Visit(m, n, node->slot, next, a, b);
            continue;
        }
        int32_t x = slot->x;
        int32_t z = slot->z;
        int32_t facing = 0;
        int32_t across = -1;
        if (next != MNAV_NO_INDEX || side == 0)
        {
            Wall(m, n, Nearest(a, b, m->wanted), false);
            continue;
        }
        mnavAcross(side, &x, &z, &facing);
        if (mnavTileAt(m->navmesh, x, z, &across) == nullptr)
        {
            Wall(m, n, Nearest(a, b, m->wanted), true);
            continue;
        }
        const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[j]];
        const mnavMeshVertex* vb =
            &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]];
        bool alongZ = side == 1 || side == 3;
        Edge e = {a, b, alongZ ? va->z : va->x, alongZ ? vb->z : vb->x};
        Side(m, n, tile, node->polygon, j, &e);
    }
    return false;
}

// Writes the polygons from the start to node last into the corridor.
static int32_t Polygons(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last)
{
    int32_t count = 0;
    for (int32_t n = last; n != MNAV_NO_NODE; n = query->nodes[n].parent)
    {
        const mnavSearchNode* node = &query->nodes[n];
        query->corridor[count++] =
            (mnavPolygonId){(uint32_t)node->slot + 1, navmesh->slots[node->slot].generation,
                            (uint32_t)node->polygon};
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPolygonId swap = query->corridor[i];
        query->corridor[i] = query->corridor[count - 1 - i];
        query->corridor[count - 1 - i] = swap;
    }
    return count;
}

mnavResult mnavMoveAlongSurface(mnavQuery* query, const mnavNavmesh* navmesh,
                                const mnavQueryFilter* filter, mnavPolygonId startPolygon,
                                mnavPos3 start, mnavPos3 end, mnavMove* moveOut)
{
    if (query == nullptr || navmesh == nullptr || moveOut == nullptr || !isfinite(start.x) ||
        !isfinite(start.y) || !isfinite(start.z) || !isfinite(end.x) || !isfinite(end.z))
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckPolygon(navmesh, startPolygon);
    result = result == mnav_success ? mnavCheckFilter(filter, &usable) : result;
    if (result != mnav_success)
    {
        return result;
    }
    // A move searches the circle through its two points, widened by a
    // cell: the start lies on the circle, and a start on a corner or an
    // edge must not lose the polygons there to rounding either way.
    Ground from = {start.x, start.z};
    Ground to = {end.x, end.z};
    Ground center = {(from.x + to.x) * 0.5, (from.z + to.z) * 0.5};
    double radius = sqrt(Distance2(from, to)) * 0.5 + (double)navmesh->def.cellSize;
    Mover m = {query, navmesh, usable, to, center, radius * radius, from, Distance2(from, to),
               0,     false,   false,  1};
    // The move takes the node table a sliced search holds: that search
    // ends here, as it does when any other search begins.
    query->search.active = false;
    query->flight.active = false;
    mnavClearTable(query);
    query->nodes[0] = (mnavSearchNode){0};
    query->nodes[0].slot = (int32_t)startPolygon.slot - 1;
    query->nodes[0].polygon = (int32_t)startPolygon.polygon;
    query->nodes[0].parent = MNAV_NO_NODE;
    query->table[mnavFindNode(query, query->nodes[0].slot, query->nodes[0].polygon, 0, 0)] = 0;
    query->indexHeap[0] = 0;
    query->nodeCount = 1;
    int32_t reached = MNAV_NO_NODE;
    for (int32_t head = 0; head < m.tail && reached == MNAV_NO_NODE; ++head)
    {
        int32_t n = query->indexHeap[head];
        reached = Walk(&m, n) ? n : MNAV_NO_NODE;
    }
    int32_t last = reached != MNAV_NO_NODE ? reached : m.bestNode;
    Ground point = reached != MNAV_NO_NODE ? to : m.best;
    const mnavSearchNode* node = &query->nodes[last];
    mnavMoveEnd ended = reached != MNAV_NO_NODE ? mnav_moveReached
                        : m.outOfNodes          ? mnav_moveOutOfNodes
                        : m.bestUnloaded        ? mnav_moveNotLoaded
                                                : mnav_moveWall;
    *moveOut = (mnavMove){
        ended,
        {point.x, mnavSurfaceHeight(navmesh, node->slot, node->polygon, point.x, point.z), point.z},
        {(uint32_t)node->slot + 1, navmesh->slots[node->slot].generation, (uint32_t)node->polygon},
        query->corridor,
        Polygons(query, navmesh, last)};
    // A walk visits few polygons: its entries come out again, newest first
    // so that each one's probe still finds it, and the next search clears
    // nothing.
    for (int32_t k = query->nodeCount - 1; k >= 0; --k)
    {
        const mnavSearchNode* visited = &query->nodes[k];
        query->table[mnavFindNode(query, visited->slot, visited->polygon, 0, 0)] = MNAV_NO_NODE;
    }
    query->tableClean = true;
    return mnav_success;
}
