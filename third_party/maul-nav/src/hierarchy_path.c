// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hierarchical paths: the start and end joined to their
// clusters' transitions, A* over the transitions, and a refinement step
// by step, each step confined to one cluster; and the confined search
// within one cluster, which building the graph runs too.

#include "fpenv.h"
#include "hierarchy.h"
#include "navmesh.h"
#include "outputs.h"
#include "query.h"

#include "maul-nav/base.h"
#include "maul-nav/hierarchy.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

mnavResult mnavSearchCluster(const mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                             bool backward, int32_t cluster, mnavPolygonId polygon, mnavPos3 point)
{
    mnavResult result =
        mnavBeginPathWork(query, navmesh, &h->filter, polygon, point, polygon, point);
    if (result != mnav_success)
    {
        return result;
    }
    mnavConfineSearch(query, h->clusterOf, cluster, true, true);
    if (backward)
    {
        mnavTurnSearchBackward(query);
    }
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = mnavContinuePathWork(query, navmesh, INT32_MAX, &ended);
    }
    bool out = query->search.outOfNodes;
    query->search.active = false;
    query->flight.active = false;
    return result != mnav_success ? result : (out ? mnav_errorCapacity : mnav_success);
}

double mnavCostTo(const mnavQuery* query, const mnavTransition* v)
{
    int32_t n = query->table[mnavFindNode(query, v->slot, v->polygon, v->tag, v->low)];
    return n == MNAV_NO_NODE ? (double)INFINITY : query->nodes[n].cost;
}

// The abstract search's open list, by cost and estimate, then by index;
// index transitionCount is the end.
static double Total(const mnavHierarchy* h, const mnavGraphSearch* g, int32_t u, mnavPos3 end)
{
    if (u == h->transitionCount)
    {
        return g->costs[u];
    }
    mnavPos3 a = h->transitions[u].at;
    double dx = a.x - end.x;
    double dy = a.y - end.y;
    double dz = a.z - end.z;
    return g->costs[u] + sqrt(dx * dx + dy * dy + dz * dz) * h->scale;
}

MNAV_NODISCARD static bool Sooner(const mnavHierarchy* h, const mnavGraphSearch* g, int32_t a,
                                  int32_t b, mnavPos3 end)
{
    double ta = Total(h, g, a, end);
    double tb = Total(h, g, b, end);
    return ta != tb ? ta < tb : a < b;
}

static void Place(mnavGraphSearch* g, int32_t at, int32_t u)
{
    g->heap[at] = u;
    g->places[u] = at;
}

static void SiftUp(const mnavHierarchy* h, mnavGraphSearch* g, int32_t at, mnavPos3 end)
{
    int32_t u = g->heap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(h, g, u, g->heap[up], end))
        {
            break;
        }
        Place(g, at, g->heap[up]);
        at = up;
    }
    Place(g, at, u);
}

static int32_t Pop(const mnavHierarchy* h, mnavGraphSearch* g, mnavPos3 end)
{
    int32_t top = g->heap[0];
    int32_t last = g->heap[--g->heapCount];
    int32_t at = 0;
    while (g->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= g->heapCount)
        {
            break;
        }
        if (child + 1 < g->heapCount && Sooner(h, g, g->heap[child + 1], g->heap[child], end))
        {
            child += 1;
        }
        if (!Sooner(h, g, g->heap[child], last, end))
        {
            break;
        }
        Place(g, at, g->heap[child]);
        at = child;
    }
    if (g->heapCount > 0)
    {
        Place(g, at, last);
    }
    g->places[top] = MNAV_CLOSED;
    return top;
}

// Lowers u's cost to cost by way of parent, opening it if new.
static void Relax(const mnavHierarchy* h, mnavGraphSearch* g, int32_t u, double cost,
                  int32_t parent, mnavPos3 end)
{
    if (g->places[u] == MNAV_CLOSED || cost >= g->costs[u])
    {
        return;
    }
    g->costs[u] = cost;
    g->parents[u] = parent;
    if (g->places[u] == MNAV_NO_NODE)
    {
        Place(g, g->heapCount++, u);
    }
    SiftUp(h, g, g->places[u], end);
}

// A* over the transitions, opened with the start's costs, to the end,
// reached from each transition with its join; whether it got there.
MNAV_NODISCARD static bool SearchGraph(const mnavHierarchy* h, mnavGraphSearch* g, mnavPos3 end)
{
    int32_t goal = h->transitionCount;
    while (g->heapCount > 0)
    {
        int32_t u = Pop(h, g, end);
        if (u == goal)
        {
            return true;
        }
        for (int32_t e = h->firstEdge[u]; e < h->firstEdge[u + 1]; ++e)
        {
            Relax(h, g, h->edges[e].to, g->costs[u] + h->edges[e].cost, u, end);
        }
        // An infinite join leaves the end as it is.
        Relax(h, g, goal, g->costs[u] + g->joins[u], u, end);
    }
    return false;
}

static void ResetGraph(const mnavHierarchy* h, mnavGraphSearch* g)
{
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        g->joins[u] = (double)INFINITY;
    }
    for (int32_t u = 0; u <= h->transitionCount; ++u)
    {
        g->costs[u] = (double)INFINITY;
        g->parents[u] = -1;
        g->places[u] = MNAV_NO_NODE;
    }
    g->heapCount = 0;
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
static void JoinLinks(const mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                      int32_t cluster)
{
    mnavGraphSearch* g = &query->graph;
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        const mnavTransition* t = &h->transitions[u];
        if (t->side == 0 && t->cluster == cluster)
        {
            g->joins[u] = CostToPoint(query, navmesh, t->slot, t->polygon, t->at, &h->filter);
        }
    }
}

// Joins the end to the transitions entering its cluster, and the start to
// those leaving its own, by searches within the clusters. The end's search
// runs from the end backward: walks cost the same either way, and it
// follows off-mesh links from where they land, so a one-way link inside
// the cluster joins the transitions it serves and no other. A search out
// of nodes joins what it reached.
static mnavResult Join(const mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                       mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                       mnavPos3 end)
{
    mnavGraphSearch* g = &query->graph;
    int32_t first = h->clusterOf[startPolygon.slot - 1];
    int32_t last = h->clusterOf[endPolygon.slot - 1];
    ResetGraph(h, g);
    mnavResult result = mnavSearchCluster(h, query, navmesh, true, last, endPolygon, end);
    if (result != mnav_success && result != mnav_errorCapacity)
    {
        return result;
    }
    for (int32_t i = h->firstLeaving[last]; i < h->firstLeaving[last + 1]; ++i)
    {
        const mnavTransition* v = &h->transitions[h->leaving[i]];
        if (v->reverse >= 0)
        {
            g->joins[v->reverse] = mnavCostTo(query, v);
        }
    }
    JoinLinks(h, query, navmesh, last);
    result = mnavSearchCluster(h, query, navmesh, false, first, startPolygon, start);
    if (result != mnav_success && result != mnav_errorCapacity)
    {
        return result;
    }
    for (int32_t i = h->firstLeaving[first]; i < h->firstLeaving[first + 1]; ++i)
    {
        int32_t v = h->leaving[i];
        double cost = mnavCostTo(query, &h->transitions[v]);
        if (isfinite(cost))
        {
            Relax(h, g, v, cost, -1, end);
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
        result = mnavContinuePathWork(query, navmesh, INT32_MAX, &ended);
    }
    *foundOut = query->search.found != MNAV_NO_NODE;
    return result;
}

// One step of the abstract path: from the node the search stands on, the
// way within cluster from across any link of the transition's run, aimed
// at the transition.
static mnavResult Step(const mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                       int32_t from, const mnavTransition* t, bool* foundOut)
{
    mnavConfineSearch(query, h->clusterOf, from, true, false);
    const int32_t goal[4] = {t->slot, t->tag, t->runLow, t->runHigh};
    mnavAimSearch(query, t->at, -1, -1, goal);
    mnavResult result = Run(query, navmesh, foundOut);
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
static mnavResult Refine(const mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                         mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                         mnavPos3 end, bool* foundOut)
{
    mnavGraphSearch* g = &query->graph;
    // The transitions in order, into the abstract search's heap.
    int32_t count = 0;
    for (int32_t u = g->parents[h->transitionCount]; u >= 0; u = g->parents[u])
    {
        g->heap[count++] = u;
    }
    mnavResult result =
        mnavBeginPathWork(query, navmesh, &h->filter, startPolygon, start, endPolygon, end);
    int32_t cluster = h->clusterOf[startPolygon.slot - 1];
    *foundOut = true;
    for (int32_t i = count - 1; i >= 0 && result == mnav_success && *foundOut; --i)
    {
        const mnavTransition* t = &h->transitions[g->heap[i]];
        result = Step(h, query, navmesh, cluster, t, foundOut);
        cluster = t->cluster;
    }
    if (result != mnav_success || !*foundOut)
    {
        return result;
    }
    mnavConfineSearch(query, h->clusterOf, cluster, false, false);
    const int32_t none[4] = {-1, -1, -1, -1};
    mnavAimSearch(query, end, (int32_t)endPolygon.slot - 1, (int32_t)endPolygon.polygon, none);
    return Run(query, navmesh, foundOut);
}

MNAV_NODISCARD static bool FinitePoint(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static mnavResult CheckQuery(const mnavQuery* query, const mnavHierarchy* h,
                             const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
                             mnavPolygonId endPolygon, mnavPos3 end)
{
    if (query == nullptr || h == nullptr || navmesh == nullptr || !FinitePoint(start) ||
        !FinitePoint(end))
    {
        return mnav_errorInvalid;
    }
    if (!mnavInQueryReach(navmesh, start) || !mnavInQueryReach(navmesh, end))
    {
        return mnav_errorLimit;
    }
    if (h->navmesh == nullptr)
    {
        return mnav_errorState;
    }
    if (h->navmesh != navmesh)
    {
        return mnav_errorInvalid;
    }
    if (h->commits != navmesh->commits)
    {
        return mnav_errorState;
    }
    mnavResult result = mnavCheckPolygon(navmesh, startPolygon);
    return result == mnav_success ? mnavCheckPolygon(navmesh, endPolygon) : result;
}

// Whether two points lie within a cluster's side of each other on the
// ground.
MNAV_NODISCARD static bool Near(const mnavHierarchy* h, const mnavNavmesh* navmesh, mnavPos3 a,
                                mnavPos3 b)
{
    double side = (double)h->def.clusterTiles * (double)navmesh->def.bake.cellSize *
                  (double)navmesh->def.bake.tileCells;
    double dx = a.x - b.x;
    double dz = a.z - b.z;
    return dx * dx + dz * dz <= side * side;
}

static MNAV_NOINLINE mnavResult FindHierarchicalPathWork(mnavQuery* query,
                                                         const mnavHierarchy* hierarchy,
                                                         const mnavNavmesh* navmesh,
                                                         mnavPolygonId startPolygon, mnavPos3 start,
                                                         mnavPolygonId endPolygon, mnavPos3 end,
                                                         mnavPath* pathOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    const mnavHierarchy* h = hierarchy;
    if (pathOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckQuery(query, h, navmesh, startPolygon, start, endPolygon, end);
    if (result != mnav_success)
    {
        return result;
    }
    if (h->transitionCount > query->limits.transitions)
    {
        return mnav_errorCapacity;
    }
    int32_t first = h->clusterOf[startPolygon.slot - 1];
    if (first == h->clusterOf[endPolygon.slot - 1])
    {
        return mnavFindPathWork(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                                pathOut);
    }
    // Ends within a cluster's side of each other try the plain search
    // first, which finds the cheapest way where it has the nodes.
    if (Near(h, navmesh, start, end))
    {
        result = mnavFindPathWork(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                                  pathOut);
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
    if (!SearchGraph(h, &query->graph, end))
    {
        return mnavFindPathWork(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
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
        return mnavFindPathWork(query, navmesh, &h->filter, startPolygon, start, endPolygon, end,
                                pathOut);
    }
    return mnavFinishPathWork(query, navmesh, pathOut);
}

static MNAV_NOINLINE mnavResult
FindHierarchicalPathCounted(mnavQuery* query, const mnavHierarchy* hierarchy,
                            const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
                            mnavPolygonId endPolygon, mnavPos3 end, mnavPath* pathOut)
{
    mnavPath pathMade = {0};
    mnavResult result = mnavHostCountInvalid(
        mnavHostFirst(MNAV_HOST(query), mnavHostFirst(MNAV_HOST(hierarchy), MNAV_HOST(navmesh))),
        FindHierarchicalPathWork(query, hierarchy, navmesh, startPolygon, start, endPolygon, end,
                                 pathOut != nullptr ? &pathMade : nullptr));
    mnavGiveOutput(result, false, pathOut, &pathMade, sizeof(pathMade));
    return result;
}

mnavResult mnavQuery_FindHierarchicalPath(mnavQuery* query, const mnavHierarchy* hierarchy,
                                          const mnavNavmesh* navmesh, mnavPolygonId startPolygon,
                                          mnavPos3 start, mnavPolygonId endPolygon, mnavPos3 end,
                                          mnavPath* pathOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = FindHierarchicalPathCounted(query, hierarchy, navmesh, startPolygon, start,
                                                    endPolygon, end, pathOut);
    mnavFloatEnvLeave(env);
    return result;
}
