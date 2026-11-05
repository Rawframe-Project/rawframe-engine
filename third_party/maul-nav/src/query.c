// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query contexts: their memory, sized by their limits, and the
// table that finds a search's nodes.

#include "query.h"

#include "allocator.h"
#include "fpenv.h"

#include "maul-nav/base.h"
#include "maul-nav/hierarchy.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

void mnavDestroyQuery(mnavQuery* query)
{
    if (query == nullptr || mnavHostCalledBack(&query->host))
    {
        return;
    }
    mnavMemory* memory = &query->memory;
    size_t nodes = (size_t)query->limits.nodes;
    size_t graph = (size_t)query->limits.transitions + 1;
    mnavRelease(memory, query->graph.heap, graph, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, query->graph.places, graph, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, query->graph.parents, graph, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, query->graph.joins, graph, sizeof(double), alignof(double));
    mnavRelease(memory, query->graph.costs, graph, sizeof(double), alignof(double));
    mnavRelease(memory, query->pathLinks, nodes, sizeof(mnavPathLink), alignof(mnavPathLink));
    mnavRelease(memory, query->pathPoints, 2 * nodes + 1, sizeof(mnavPos3), alignof(mnavPos3));
    mnavRelease(memory, query->pathPolygons, nodes, sizeof(mnavPolygonId), alignof(mnavPolygonId));
    mnavRelease(memory, query->links, nodes, sizeof(mnavPathLink), alignof(mnavPathLink));
    mnavRelease(memory, query->points, 2 * nodes + 1, sizeof(mnavPos3), alignof(mnavPos3));
    mnavRelease(memory, query->portals, 2 * nodes + 1, sizeof(mnavPortal), alignof(mnavPortal));
    mnavRelease(memory, query->corridor, nodes, sizeof(mnavPolygonId), alignof(mnavPolygonId));
    mnavRelease(memory, query->table, (size_t)query->tableMask + 1, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(memory, query->heap, nodes, sizeof(mnavHeapEntry), alignof(mnavHeapEntry));
    mnavRelease(memory, query->nodes, nodes, sizeof(mnavSearchNode), alignof(mnavSearchNode));
    // The mark lives in the block being freed: it is set for good, so that
    // a call back in from this release is refused too, and the release
    // does not touch it.
    mnavHostEnter(&query->host);
    mnavMemory last = *memory;
    last.host = nullptr;
    mnavRelease(&last, query, 1, sizeof(mnavQuery), alignof(mnavQuery));
}

uint64_t mnavQuery_GetMisuse(const mnavQuery* query)
{
    return query != nullptr ? mnavHostMisuse(&query->host) : 0;
}
// The kept path's own arrays, as large as the ones a search writes.
static mnavResult AllocatePath(mnavQuery* query)
{
    size_t nodes = (size_t)query->limits.nodes;
    mnavResult result = mnavAllocate(&query->memory, nodes, sizeof(mnavPolygonId),
                                     alignof(mnavPolygonId), (void**)&query->pathPolygons);
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, 2 * nodes + 1, sizeof(mnavPos3), alignof(mnavPos3),
                              (void**)&query->pathPoints);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, nodes, sizeof(mnavPathLink), alignof(mnavPathLink),
                              (void**)&query->pathLinks);
    }
    return result;
}

// The search over a hierarchy's transitions, one entry more for the end.
static mnavResult AllocateGraph(mnavQuery* query)
{
    size_t graph = (size_t)query->limits.transitions + 1;
    mnavGraphSearch* g = &query->graph;
    mnavResult result =
        mnavAllocate(&query->memory, graph, sizeof(double), alignof(double), (void**)&g->costs);
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&query->memory, graph, sizeof(double), alignof(double), (void**)&g->joins);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, graph, sizeof(int32_t), alignof(int32_t),
                              (void**)&g->parents);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, graph, sizeof(int32_t), alignof(int32_t),
                              (void**)&g->places);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, graph, sizeof(int32_t), alignof(int32_t),
                              (void**)&g->heap);
    }
    return result;
}

static mnavResult CheckLimits(const mnavQueryLimits* limits)
{
    if (limits->nodes < 1 || !(limits->pathLength > 0.0f) || limits->transitions < 0)
    {
        return mnav_errorInvalid;
    }
    if (limits->nodes > MNAV_MAX_QUERY_NODES || limits->pathLength > MNAV_MAX_PATH_LENGTH ||
        limits->transitions > MNAV_MAX_HIERARCHY_TRANSITIONS)
    {
        return mnav_errorLimit;
    }
    return mnav_success;
}

// The def's cookie, its version, then its allocator and its limits.
static mnavResult CheckDef(const mnavQueryDef* def)
{
    if (def == nullptr || def->cookie != MNAV_QUERY_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    return CheckLimits(&def->limits);
}

static MNAV_NOINLINE mnavResult CreateQueryWork(const mnavQueryDef* def, mnavQuery** queryOut)
{
    if (queryOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *queryOut = nullptr;
    mnavResult checked = CheckDef(def);
    if (checked != mnav_success)
    {
        return checked;
    }
    const mnavQueryLimits* limits = &def->limits;
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavQuery* query = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavQuery), alignof(mnavQuery), (void**)&query);
    if (result != mnav_success)
    {
        return result;
    }
    *query = (mnavQuery){.memory = memory};
    query->memory.host = &query->host;
    query->limits = *limits;
    // A table at least twice the nodes keeps probes short.
    uint32_t table = 2;
    while (table < 2u * (uint32_t)limits->nodes)
    {
        table *= 2;
    }
    query->tableMask = table - 1;
    size_t nodes = (size_t)limits->nodes;
    result = mnavAllocate(&query->memory, nodes, sizeof(mnavSearchNode), alignof(mnavSearchNode),
                          (void**)&query->nodes);
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, nodes, sizeof(mnavHeapEntry), alignof(mnavHeapEntry),
                              (void**)&query->heap);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, table, sizeof(int32_t), alignof(int32_t),
                              (void**)&query->table);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, nodes, sizeof(mnavPolygonId), alignof(mnavPolygonId),
                              (void**)&query->corridor);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, 2 * nodes + 1, sizeof(mnavPortal),
                              alignof(mnavPortal), (void**)&query->portals);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, 2 * nodes + 1, sizeof(mnavPos3), alignof(mnavPos3),
                              (void**)&query->points);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&query->memory, nodes, sizeof(mnavPathLink), alignof(mnavPathLink),
                              (void**)&query->links);
    }
    result = result == mnav_success ? AllocatePath(query) : result;
    result = result == mnav_success ? AllocateGraph(query) : result;
    if (result != mnav_success)
    {
        mnavDestroyQuery(query);
        return result;
    }
    *queryOut = query;
    return mnav_success;
}

mnavResult mnavCreateQuery(const mnavQueryDef* def, mnavQuery** queryOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = CreateQueryWork(def, queryOut);
    mnavFloatEnvLeave(env);
    return result;
}

static uint32_t Hash(int32_t slot, int32_t polygon, int32_t tag, int32_t low)
{
    uint64_t h = (uint64_t)(uint32_t)slot * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)polygon * 0xC2B2AE3D27D4EB4Full;
    h ^= ((uint64_t)(uint32_t)tag << 32 | (uint32_t)low) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    return (uint32_t)(h ^ (h >> 32));
}

void mnavClearTable(mnavQuery* query)
{
    if (!query->tableClean)
    {
        memset(query->table, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    }
    query->tableClean = false;
}

// The table cell holding the node with a key, or the empty cell where it
// would go.
uint32_t mnavFindNode(const mnavQuery* query, int32_t slot, int32_t polygon, int32_t tag,
                      int32_t low)
{
    uint32_t cell = Hash(slot, polygon, tag, low) & query->tableMask;
    for (;;)
    {
        int32_t n = query->table[cell];
        if (n == MNAV_NO_NODE)
        {
            return cell;
        }
        const mnavSearchNode* node = &query->nodes[n];
        if (node->slot == slot && node->polygon == polygon && node->tag == tag && node->low == low)
        {
            return cell;
        }
        cell = (cell + 1) & query->tableMask;
    }
}

void mnavKeepPath(mnavQuery* query, mnavPath* path)
{
    // A search's counts are within the arrays it wrote, which these match.
    if (path->polygonCount > 0)
    {
        memcpy(query->pathPolygons, path->polygons,
               (size_t)path->polygonCount * sizeof(mnavPolygonId));
        path->polygons = query->pathPolygons;
    }
    if (path->pointCount > 0)
    {
        memcpy(query->pathPoints, path->points, (size_t)path->pointCount * sizeof(mnavPos3));
        path->points = query->pathPoints;
    }
    if (path->linkCount > 0)
    {
        memcpy(query->pathLinks, path->links, (size_t)path->linkCount * sizeof(mnavPathLink));
        path->links = query->pathLinks;
    }
    query->pathPolygonCount = path->polygonCount;
    query->pathPointCount = path->pointCount;
    query->pathLinkCount = path->linkCount;
    query->generation += 1;
    path->query = query;
    path->generation = query->generation;
}

const mnavPos3* mnavKeepPoints(mnavQuery* query, const mnavPos3* points, int32_t count)
{
    if (count > 0)
    {
        memcpy(query->pathPoints, points, (size_t)count * sizeof(mnavPos3));
    }
    query->pathPolygonCount = 0;
    query->pathPointCount = count;
    query->pathLinkCount = 0;
    query->generation += 1;
    return count > 0 ? query->pathPoints : points;
}
