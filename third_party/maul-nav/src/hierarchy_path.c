// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hierarchical paths (mnav-0008): the start and end joined to their
// clusters' transitions, A* over the transitions, and a refinement step
// by step, each step confined to one cluster.

#include "hierarchy.h"
#include "navmesh.h"
#include "query.h"

#include "maul-nav/base.h"
#include "maul-nav/hierarchy.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// The abstract search's open list, by cost and estimate, then by index;
// index transitionCount is the end.
static double Total(const mnavHierarchy* h, int32_t u, mnavPos3 end)
{
    if (u == h->transitionCount)
    {
        return h->costs[u];
    }
    mnavPos3 a = h->transitions[u].at;
    double dx = a.x - end.x;
    double dy = a.y - end.y;
    double dz = a.z - end.z;
    return h->costs[u] + sqrt(dx * dx + dy * dy + dz * dz) * h->scale;
}

static bool Sooner(const mnavHierarchy* h, int32_t a, int32_t b, mnavPos3 end)
{
    double ta = Total(h, a, end);
    double tb = Total(h, b, end);
    return ta != tb ? ta < tb : a < b;
}

static void Place(mnavHierarchy* h, int32_t at, int32_t u)
{
    h->heap[at] = u;
    h->places[u] = at;
}

static void SiftUp(mnavHierarchy* h, int32_t at, mnavPos3 end)
{
    int32_t u = h->heap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(h, u, h->heap[up], end))
        {
            break;
        }
        Place(h, at, h->heap[up]);
        at = up;
    }
    Place(h, at, u);
}

static int32_t Pop(mnavHierarchy* h, mnavPos3 end)
{
    int32_t top = h->heap[0];
    int32_t last = h->heap[--h->heapCount];
    int32_t at = 0;
    while (h->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= h->heapCount)
        {
            break;
        }
        if (child + 1 < h->heapCount && Sooner(h, h->heap[child + 1], h->heap[child], end))
        {
            child += 1;
        }
        if (!Sooner(h, h->heap[child], last, end))
        {
            break;
        }
        Place(h, at, h->heap[child]);
        at = child;
    }
    if (h->heapCount > 0)
    {
        Place(h, at, last);
    }
    h->places[top] = MNAV_CLOSED;
    return top;
}

// Lowers u's cost to cost by way of parent, opening it if new.
static void Relax(mnavHierarchy* h, int32_t u, double cost, int32_t parent, mnavPos3 end)
{
    if (h->places[u] == MNAV_CLOSED || cost >= h->costs[u])
    {
        return;
    }
    h->costs[u] = cost;
    h->parents[u] = parent;
    if (h->places[u] == MNAV_NO_NODE)
    {
        Place(h, h->heapCount++, u);
    }
    SiftUp(h, h->places[u], end);
}

// A* over the transitions, opened with the start's costs, to the end,
// reached from each transition with its join; whether it got there.
static bool SearchGraph(mnavHierarchy* h, mnavPos3 end)
{
    int32_t goal = h->transitionCount;
    while (h->heapCount > 0)
    {
        int32_t u = Pop(h, end);
        if (u == goal)
        {
            return true;
        }
        for (int32_t e = h->firstEdge[u]; e < h->firstEdge[u + 1]; ++e)
        {
            Relax(h, h->edges[e].to, h->costs[u] + h->edges[e].cost, u, end);
        }
        // An infinite join leaves the end as it is.
        Relax(h, goal, h->costs[u] + h->joins[u], u, end);
    }
    return false;
}

static void ResetGraph(mnavHierarchy* h)
{
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        h->joins[u] = (double)INFINITY;
    }
    for (int32_t u = 0; u <= h->transitionCount; ++u)
    {
        h->costs[u] = (double)INFINITY;
        h->parents[u] = -1;
        h->places[u] = MNAV_NO_NODE;
    }
    h->heapCount = 0;
}

// The cost the last search found to a point in a polygon: through the
// cheapest node in it, then straight to the point.
static double CostToPoint(const mnavQuery* query, const mnavNavmesh* navmesh, int32_t slot,
                          int32_t polygon, mnavPos3 point, const mnavQueryFilter* filter)
{
    const mnavTile* tile = navmesh->slots[slot].tile;
    double perMeter = (double)filter->costs[tile->mesh.polygons[polygon].area];
    double best = (double)INFINITY;
    for (int32_t n = 0; n < query->nodeCount; ++n)
    {
        const mnavSearchNode* node = &query->nodes[n];
        if (node->slot != slot || node->polygon != polygon)
        {
            continue;
        }
        double dx = node->at.x - point.x;
        double dy = node->at.y - point.y;
        double dz = node->at.z - point.z;
        double cost = node->cost + sqrt(dx * dx + dy * dy + dz * dz) * perMeter;
        best = cost < best ? cost : best;
    }
    return best;
}

// Joins the end to the off-mesh links landing in its cluster: the walk
// from each landing, found by the search from the end.
static void JoinLinks(mnavHierarchy* h, const mnavQuery* query, const mnavNavmesh* navmesh,
                      int32_t cluster)
{
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        const mnavTransition* t = &h->transitions[u];
        if (t->side == 0 && t->cluster == cluster)
        {
            h->joins[u] = CostToPoint(query, navmesh, t->slot, t->polygon, t->at, &h->filter);
        }
    }
}

// Joins the end to the transitions entering its cluster, and the start to
// those leaving its own, by searches within the clusters; the walk's cost
// is the same either way, so the end's search runs from the end. A search
// out of nodes joins what it reached.
static mnavResult Join(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                       mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                       mnavPos3 end)
{
    int32_t first = h->clusterOf[startPolygon.slot - 1];
    int32_t last = h->clusterOf[endPolygon.slot - 1];
    ResetGraph(h);
    mnavResult result = mnavSearchCluster(h, query, navmesh, last, endPolygon, end);
    if (result != mnav_success && result != mnav_errorLimit)
    {
        return result;
    }
    for (int32_t i = h->firstLeaving[last]; i < h->firstLeaving[last + 1]; ++i)
    {
        const mnavTransition* v = &h->transitions[h->leaving[i]];
        if (v->reverse >= 0)
        {
            h->joins[v->reverse] = mnavCostTo(query, v);
        }
    }
    JoinLinks(h, query, navmesh, last);
    result = mnavSearchCluster(h, query, navmesh, first, startPolygon, start);
    if (result != mnav_success && result != mnav_errorLimit)
    {
        return result;
    }
    for (int32_t i = h->firstLeaving[first]; i < h->firstLeaving[first + 1]; ++i)
    {
        int32_t v = h->leaving[i];
        double cost = mnavCostTo(query, &h->transitions[v]);
        if (isfinite(cost))
        {
            Relax(h, v, cost, -1, end);
        }
    }
    return mnav_success;
}

// Runs the search begun until it ends; whether it found its end.
static mnavResult Run(mnavQuery* query, const mnavNavmesh* navmesh, bool* foundOut)
{
    mnavResult result = mnav_success;
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = mnavContinuePath(query, navmesh, INT32_MAX, &ended);
    }
    *foundOut = query->search.found != MNAV_NO_NODE;
    return result;
}

// One step of the abstract path: from the node the search stands on, the
// way within cluster from across any link of the transition's run, aimed
// at the transition.
static mnavResult Step(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh, int32_t from,
                       const mnavTransition* t, bool* foundOut)
{
    mnavMarkCluster(h, from, 1);
    mnavConfineSearch(query, h->inside, true, false);
    const int32_t goal[4] = {t->slot, t->tag, t->runLow, t->runHigh};
    mnavAimSearch(query, t->at, -1, -1, goal);
    mnavResult result = Run(query, navmesh, foundOut);
    mnavMarkCluster(h, from, 0);
    if (result == mnav_success && *foundOut)
    {
        mnavRestartSearch(query, query->search.found);
    }
    return result;
}

// Refines the abstract path step by step, each step's search confined to
// one cluster and started over from where the last one ended, so that
// the nodes held are the way so far and one cluster's; then the last
// cluster to the end. Whether every step found its way.
static mnavResult Refine(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                         mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                         mnavPos3 end, bool* foundOut)
{
    // The transitions in order, into the abstract search's heap.
    int32_t count = 0;
    for (int32_t u = h->parents[h->transitionCount]; u >= 0; u = h->parents[u])
    {
        h->heap[count++] = u;
    }
    mnavResult result =
        mnavBeginPath(query, navmesh, &h->filter, startPolygon, start, endPolygon, end);
    int32_t cluster = h->clusterOf[startPolygon.slot - 1];
    *foundOut = true;
    for (int32_t i = count - 1; i >= 0 && result == mnav_success && *foundOut; --i)
    {
        const mnavTransition* t = &h->transitions[h->heap[i]];
        result = Step(h, query, navmesh, cluster, t, foundOut);
        cluster = t->cluster;
    }
    if (result != mnav_success || !*foundOut)
    {
        return result;
    }
    mnavMarkCluster(h, cluster, 1);
    mnavConfineSearch(query, h->inside, false, false);
    const int32_t none[4] = {-1, -1, -1, -1};
    mnavAimSearch(query, end, (int32_t)endPolygon.slot - 1, (int32_t)endPolygon.polygon, none);
    result = Run(query, navmesh, foundOut);
    mnavMarkCluster(h, cluster, 0);
    return result;
}

static bool FinitePoint(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static mnavResult CheckQuery(const mnavQuery* query, const mnavHierarchy* h,
                             const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
                             mnavPolygonId endPolygon, mnavPos3 end)
{
    if (query == nullptr || h == nullptr || navmesh == nullptr || !FinitePoint(start) ||
        !FinitePoint(end) || h->navmesh != navmesh)
    {
        return mnav_errorInvalid;
    }
    if (h->commits != navmesh->commits)
    {
        return mnav_errorStale;
    }
    mnavResult result = mnavCheckPolygon(navmesh, startPolygon);
    return result == mnav_success ? mnavCheckPolygon(navmesh, endPolygon) : result;
}

// Whether two points lie within a cluster's side of each other on the
// ground.
static bool Near(const mnavHierarchy* h, const mnavNavmesh* navmesh, mnavPos3 a, mnavPos3 b)
{
    double side = (double)h->def.clusterTiles * (double)navmesh->def.cellSize *
                  (double)navmesh->def.tileCells;
    double dx = a.x - b.x;
    double dz = a.z - b.z;
    return dx * dx + dz * dz <= side * side;
}

mnavResult mnavFindHierarchicalPath(mnavQuery* query, mnavHierarchy* hierarchy,
                                    const mnavNavmesh* navmesh, mnavPolygonId startPolygon,
                                    mnavPos3 start, mnavPolygonId endPolygon, mnavPos3 end,
                                    mnavPath* pathOut)
{
    mnavHierarchy* h = hierarchy;
    if (pathOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckQuery(query, h, navmesh, startPolygon, start, endPolygon, end);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t first = h->clusterOf[startPolygon.slot - 1];
    if (first == h->clusterOf[endPolygon.slot - 1])
    {
        return mnavFindPath(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                            pathOut);
    }
    // Ends within a cluster's side of each other try the plain search
    // first, which finds the cheapest way where it has the nodes.
    if (Near(h, navmesh, start, end))
    {
        result =
            mnavFindPath(query, navmesh, &h->filter, startPolygon, start, endPolygon, end, pathOut);
        if (result != mnav_success || pathOut->end != mnav_pathOutOfNodes)
        {
            return result;
        }
    }
    result = Join(h, query, navmesh, startPolygon, start, endPolygon, end);
    if (result != mnav_success)
    {
        return result;
    }
    if (!SearchGraph(h, end))
    {
        return mnavFindPath(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                            pathOut);
    }
    bool found = false;
    result = Refine(h, query, navmesh, startPolygon, start, endPolygon, end, &found);
    if (result != mnav_success)
    {
        return result;
    }
    if (!found)
    {
        return mnavFindPath(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                            pathOut);
    }
    return mnavFinishPath(query, navmesh, pathOut);
}
