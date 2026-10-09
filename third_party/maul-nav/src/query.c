// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query contexts (mnav-0005): their memory, sized by their limits, and the
// table that finds a search's nodes.

#include "query.h"

#include "allocator.h"

#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <stdint.h>

// Marks a def built by mnavDefaultQueryDef.
#define QUERY_DEF_COOKIE 0x4E415651u

mnavQueryDef mnavDefaultQueryDef(void)
{
    mnavQueryDef def = {0};
    def.cookie = QUERY_DEF_COOKIE;
    def.limits.nodes = 8192;
    def.limits.pathLength = 1000.0f;
    return def;
}

void mnavDestroyQuery(mnavQuery* query)
{
    if (query == nullptr)
    {
        return;
    }
    mnavMemory* memory = &query->memory;
    size_t nodes = (size_t)query->limits.nodes;
    mnavRelease(memory, query->links, nodes, sizeof(mnavPathLink), alignof(mnavPathLink));
    mnavRelease(memory, query->points, 2 * nodes + 1, sizeof(mnavPos3), alignof(mnavPos3));
    mnavRelease(memory, query->portals, 2 * nodes + 1, sizeof(mnavPortal), alignof(mnavPortal));
    mnavRelease(memory, query->corridor, nodes, sizeof(mnavPolygonId), alignof(mnavPolygonId));
    mnavRelease(memory, query->table, (size_t)query->tableMask + 1, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(memory, query->heap, nodes, sizeof(mnavHeapEntry), alignof(mnavHeapEntry));
    mnavRelease(memory, query->nodes, nodes, sizeof(mnavSearchNode), alignof(mnavSearchNode));
    mnavMemory last = *memory;
    mnavRelease(&last, query, 1, sizeof(mnavQuery), alignof(mnavQuery));
}

mnavResult mnavCreateQuery(const mnavQueryDef* def, mnavQuery** queryOut)
{
    if (queryOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *queryOut = nullptr;
    if (def == nullptr || def->cookie != QUERY_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    const mnavQueryLimits* limits = &def->limits;
    if (limits->nodes < 1 || limits->nodes > MNAV_MAX_QUERY_NODES ||
        !(limits->pathLength > 0.0f && limits->pathLength <= MNAV_MAX_PATH_LENGTH))
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavQuery* query = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavQuery), alignof(mnavQuery), (void**)&query);
    if (result != mnav_success)
    {
        return result;
    }
    *query = (mnavQuery){0};
    query->memory = memory;
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
    if (result != mnav_success)
    {
        mnavDestroyQuery(query);
        return result;
    }
    *queryOut = query;
    return mnav_success;
}

static uint32_t Hash(int32_t slot, int32_t polygon, int32_t tag, int32_t low)
{
    uint64_t h = (uint64_t)(uint32_t)slot * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uint32_t)polygon * 0xC2B2AE3D27D4EB4Full;
    h ^= ((uint64_t)(uint32_t)tag << 32 | (uint32_t)low) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    return (uint32_t)(h ^ (h >> 32));
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
