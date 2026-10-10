// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// From a search's last node to its corridor and straight path: the funnel
// algorithm, stretch by stretch between off-mesh links (mnav-0005).

#include "funnel.h"

#include "navmesh.h"
#include "query.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

// Writes the polygons from the start to node last into the corridor.
int32_t mnavPathPolygons(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last)
{
    int32_t count = 0;
    // Nodes in one polygon are one visit, except across an off-mesh link:
    // a link landing on the polygon it took off from leaves it and comes
    // back, two visits the corridor keeps apart.
    bool crossed = false;
    for (int32_t n = last; n != MNAV_NO_NODE; n = query->nodes[n].parent)
    {
        const mnavSearchNode* node = &query->nodes[n];
        mnavPolygonId id = {(uint32_t)node->slot + 1, navmesh->slots[node->slot].generation,
                            (uint32_t)node->polygon};
        const mnavPolygonId* previous = count > 0 ? &query->corridor[count - 1] : nullptr;
        if (previous == nullptr || crossed || previous->slot != id.slot ||
            previous->polygon != id.polygon)
        {
            query->corridor[count++] = id;
        }
        crossed = node->tag == MNAV_TAG_OFFMESH;
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPolygonId swap = query->corridor[i];
        query->corridor[i] = query->corridor[count - 1 - i];
        query->corridor[count - 1 - i] = swap;
    }
    return count;
}

// Writes the portals from the start point to node last, and the end point
// or, short of it, the last portal's midpoint as a portal of one point.
static int32_t Portals(mnavQuery* query, int32_t last)
{
    int32_t count = 0;
    for (int32_t n = last; n != MNAV_NO_NODE; n = query->nodes[n].parent)
    {
        // A portal's ends are in the order of the polygon walked out of,
        // whose inside lies to the right of each edge: the first end is on
        // the walker's left. An off-mesh link gives its takeoff point, then
        // its landing point; written backward here.
        const mnavSearchNode* node = &query->nodes[n];
        if (node->tag == MNAV_TAG_OFFMESH)
        {
            query->portals[count++] = (mnavPortal){node->b, node->b, -1};
            query->portals[count++] = (mnavPortal){node->a, node->a, node->low};
            continue;
        }
        query->portals[count++] = (mnavPortal){node->a, node->b, -1};
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPortal swap = query->portals[i];
        query->portals[i] = query->portals[count - 1 - i];
        query->portals[count - 1 - i] = swap;
    }
    const mnavSearchNode* node = &query->nodes[last];
    if (node->tag != MNAV_TAG_END)
    {
        query->portals[count++] = (mnavPortal){node->at, node->at, -1};
    }
    return count;
}

// Twice the signed area of triangle abc on the ground: positive when c lies
// to the left of the way from a to b.
static double Area2(mnavPos3 a, mnavPos3 b, mnavPos3 c)
{
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

static bool Same(mnavPos3 a, mnavPos3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static void Push(mnavQuery* query, int32_t* count, mnavPos3 p)
{
    if (*count == 0 || !Same(query->points[*count - 1], p))
    {
        query->points[(*count)++] = p;
    }
}

// The funnel: its apex and two sides, each with the portal it came from.
typedef struct Funnel
{
    mnavPos3 apex;
    mnavPos3 left;
    mnavPos3 right;
    int32_t apexAt;
    int32_t leftAt;
    int32_t rightAt;
} Funnel;

// Whether a point lies on a portal, within a millimeter on the ground.
static bool OnPortal(mnavPos3 p, const mnavPortal* portal)
{
    double dx = portal->right.x - portal->left.x;
    double dz = portal->right.z - portal->left.z;
    double length2 = dx * dx + dz * dz;
    double t =
        length2 > 0.0 ? ((p.x - portal->left.x) * dx + (p.z - portal->left.z) * dz) / length2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    double ex = portal->left.x + dx * t - p.x;
    double ez = portal->left.z + dz * t - p.z;
    return ex * ex + ez * ez < 1e-6;
}

// Makes a side's point the funnel's new apex, a corner of the path.
static void Corner(mnavQuery* query, int32_t* count, Funnel* f, mnavPos3 p, int32_t at)
{
    Push(query, count, p);
    *f = (Funnel){p, p, p, at, at, at};
}

// Pulls portals first to last tight into a straight path: a portal end on
// or inside a side of the funnel narrows it; one on or past the other side
// makes that side's point a corner, and the scan goes on from there. The
// first portal is a point, always kept: the start or a landing point. A
// portal the apex lies on, as at a seam or a vertex, is crossed already
// and constrains nothing: it is passed over, as Detour passes over the
// first when the start lies on it.
static int32_t Pull(mnavQuery* query, int32_t first, int32_t last, int32_t count)
{
    const mnavPortal* p = query->portals;
    query->points[count++] = p[first].left;
    Funnel f = {p[first].left, p[first].left, p[first].right, first, first, first};
    for (int32_t i = first + 1; i <= last; ++i)
    {
        if (OnPortal(f.apex, &p[i]))
        {
            continue;
        }
        if (Area2(f.apex, f.right, p[i].right) >= 0.0)
        {
            if (!Same(f.apex, f.right) && Area2(f.apex, f.left, p[i].right) >= 0.0)
            {
                Corner(query, &count, &f, f.left, f.leftAt);
                i = f.apexAt;
                continue;
            }
            f.right = p[i].right;
            f.rightAt = i;
        }
        if (Area2(f.apex, f.left, p[i].left) <= 0.0)
        {
            if (!Same(f.apex, f.left) && Area2(f.apex, f.right, p[i].left) <= 0.0)
            {
                Corner(query, &count, &f, f.right, f.rightAt);
                i = f.apexAt;
                continue;
            }
            f.left = p[i].left;
            f.leftAt = i;
        }
    }
    Push(query, &count, p[last].left);
    return count;
}

// The straight path, stretch by stretch between off-mesh links: each
// stretch ends at a takeoff point, the next begins at its landing point.
int32_t mnavStraighten(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last,
                       int32_t* linkCount)
{
    return mnavPullPortals(query, navmesh, Portals(query, last), linkCount);
}

int32_t mnavPullPortals(mnavQuery* query, const mnavNavmesh* navmesh, int32_t portals,
                        int32_t* linkCount)
{
    int32_t count = 0;
    int32_t first = 0;
    *linkCount = 0;
    for (int32_t i = 0; i < portals; ++i)
    {
        int32_t crossed = query->portals[i].link;
        if (crossed < 0 && i + 1 < portals)
        {
            continue;
        }
        count = Pull(query, first, i, count);
        first = i + 1;
        if (crossed >= 0)
        {
            // An edge link's crossing is reported as the link its id names.
            const mnavOffLink* link = &navmesh->links[navmesh->links[crossed / 2].parent];
            query->links[(*linkCount)++] =
                (mnavPathLink){{(uint32_t)navmesh->links[crossed / 2].parent + 1, link->generation},
                               link->def.kind,
                               count - 1};
        }
    }
    return count;
}
