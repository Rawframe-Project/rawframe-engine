// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The search (path_search.h): a binary heap of probes keyed by cost and
// index; the probes a search touches are listed and only they reset, so
// a search costs what it visits, not the set's size.

#include "path_search.h"

#include "allocator.h"

#include <math.h>
#include <string.h>

// No probe; a probe whose cost is final.
#define NONE    UINT32_MAX
#define SETTLED (UINT32_MAX - 1u)

static float Distance(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

bool maudCreatePathSearch(const maudAllocator* allocator, uint32_t capacity, maudPathSearch* search)
{
    *search = (maudPathSearch){.capacity = capacity};
    maudLayout layout = {0};
    size_t cost = maudLayoutAdd(&layout, capacity, sizeof(float), alignof(float));
    size_t parent = maudLayoutAdd(&layout, capacity, sizeof(uint32_t), alignof(uint32_t));
    size_t heap = maudLayoutAdd(&layout, capacity, sizeof(uint32_t), alignof(uint32_t));
    size_t position = maudLayoutAdd(&layout, capacity, sizeof(uint32_t), alignof(uint32_t));
    size_t touched = maudLayoutAdd(&layout, capacity, sizeof(uint32_t), alignof(uint32_t));
    if (layout.overflow)
    {
        return false;
    }
    search->bytes = layout.size;
    search->memory = maudAllocate(allocator, search->bytes, alignof(float));
    if (search->memory == nullptr)
    {
        return false;
    }
    unsigned char* base = search->memory;
    search->cost = (float*)(base + cost);
    search->parent = (uint32_t*)(base + parent);
    search->heap = (uint32_t*)(base + heap);
    search->position = (uint32_t*)(base + position);
    search->touched = (uint32_t*)(base + touched);
    for (uint32_t i = 0; i < capacity; ++i)
    {
        search->cost[i] = INFINITY;
        search->position[i] = NONE;
    }
    return true;
}

void maudDestroyPathSearch(const maudAllocator* allocator, maudPathSearch* search)
{
    if (search->memory != nullptr)
    {
        maudRelease(allocator, search->memory, search->bytes, alignof(float));
    }
    *search = (maudPathSearch){0};
}

void maudAttachPath(const maudProbeGraph* graph, float range, maudVector3 point,
                    maudAnyHitFn* anyHit, void* context, maudPathEnd* end)
{
    uint32_t nearest[MAUD_PATH_CANDIDATES];
    float lengths[MAUD_PATH_CANDIDATES];
    uint32_t n = 0;
    for (uint32_t i = 0; i < graph->count; ++i)
    {
        float d = Distance(point, graph->points[i]);
        if (d > range || (n == MAUD_PATH_CANDIDATES && d >= lengths[n - 1]))
        {
            continue;
        }
        uint32_t k = n < MAUD_PATH_CANDIDATES ? n++ : n - 1;
        for (; k > 0 && lengths[k - 1] > d; --k)
        {
            nearest[k] = nearest[k - 1];
            lengths[k] = lengths[k - 1];
        }
        nearest[k] = i;
        lengths[k] = d;
    }
    maudRay rays[MAUD_PATH_CANDIDATES];
    uint8_t occluded[MAUD_PATH_CANDIDATES] = {0};
    for (uint32_t k = 0; k < n; ++k)
    {
        maudVector3 to = graph->points[nearest[k]];
        float d = lengths[k];
        rays[k] = (maudRay){point,
                            d > 0.0f ? (maudVector3){(to.x - point.x) / d, (to.y - point.y) / d,
                                                     (to.z - point.z) / d}
                                     : (maudVector3){0.0f, 1.0f, 0.0f},
                            0.0f, d};
    }
    if (anyHit != nullptr && n > 0)
    {
        anyHit(rays, n, occluded, context);
    }
    end->count = 0;
    for (uint32_t k = 0; k < n && end->count < MAUD_PATH_ATTACH; ++k)
    {
        if (occluded[k] == 0)
        {
            end->probes[end->count] = nearest[k];
            end->lengths[end->count] = lengths[k];
            end->count += 1;
        }
    }
}

static bool Less(const maudPathSearch* s, uint32_t a, uint32_t b)
{
    return s->cost[a] < s->cost[b] || (s->cost[a] == s->cost[b] && a < b);
}

static void Place(maudPathSearch* s, uint32_t slot, uint32_t probe)
{
    s->heap[slot] = probe;
    s->position[probe] = slot;
}

static void Up(maudPathSearch* s, uint32_t slot)
{
    uint32_t probe = s->heap[slot];
    while (slot > 0 && Less(s, probe, s->heap[(slot - 1) / 2]))
    {
        Place(s, slot, s->heap[(slot - 1) / 2]);
        slot = (slot - 1) / 2;
    }
    Place(s, slot, probe);
}

static void Down(maudPathSearch* s, uint32_t slot, uint32_t size)
{
    uint32_t probe = s->heap[slot];
    for (;;)
    {
        uint32_t child = 2 * slot + 1;
        if (child >= size)
        {
            break;
        }
        if (child + 1 < size && Less(s, s->heap[child + 1], s->heap[child]))
        {
            child += 1;
        }
        if (!Less(s, s->heap[child], probe))
        {
            break;
        }
        Place(s, slot, s->heap[child]);
        slot = child;
    }
    Place(s, slot, probe);
}

// Lowers a probe's cost, entering it in the heap if it was not.
static void Relax(maudPathSearch* s, uint32_t* size, uint32_t probe, float cost, uint32_t parent)
{
    if (s->position[probe] == SETTLED || !(cost < s->cost[probe]))
    {
        return;
    }
    if (s->cost[probe] == INFINITY)
    {
        s->touched[s->touchedCount++] = probe;
    }
    s->cost[probe] = cost;
    s->parent[probe] = parent;
    if (s->position[probe] == NONE)
    {
        Place(s, *size, probe);
        *size += 1;
    }
    Up(s, s->position[probe]);
}

static uint32_t Pop(maudPathSearch* s, uint32_t* size)
{
    uint32_t probe = s->heap[0];
    *size -= 1;
    if (*size > 0)
    {
        Place(s, 0, s->heap[*size]);
        Down(s, 0, *size);
    }
    s->position[probe] = SETTLED;
    return probe;
}

static bool Blocked(const maudPathSearch* s, uint32_t a, uint32_t b)
{
    uint32_t low = a < b ? a : b;
    uint32_t high = a < b ? b : a;
    for (uint32_t k = 0; k < s->blockedCount; ++k)
    {
        if (s->blocked[2 * k] == low && s->blocked[2 * k + 1] == high)
        {
            return true;
        }
    }
    return false;
}

static void Reset(maudPathSearch* s)
{
    for (uint32_t k = 0; k < s->touchedCount; ++k)
    {
        s->cost[s->touched[k]] = INFINITY;
        s->position[s->touched[k]] = NONE;
    }
    s->touchedCount = 0;
}

// Dijkstra's search from the source's probes; returns the listener's
// probe that ends the shortest path, or NONE.
static uint32_t Run(maudPathSearch* s, const maudProbeGraph* g, const maudPathEnd* source,
                    const maudPathEnd* listener)
{
    uint32_t size = 0;
    for (uint32_t k = 0; k < source->count; ++k)
    {
        Relax(s, &size, source->probes[k], source->lengths[k], NONE);
    }
    float best = INFINITY;
    uint32_t last = NONE;
    while (size > 0)
    {
        uint32_t u = Pop(s, &size);
        if (!(s->cost[u] < best))
        {
            break;
        }
        for (uint32_t k = 0; k < listener->count; ++k)
        {
            float total = s->cost[u] + listener->lengths[k];
            if (listener->probes[k] == u && total < best)
            {
                best = total;
                last = u;
            }
        }
        for (uint32_t e = g->offsets[u]; e < g->offsets[u + 1]; ++e)
        {
            uint32_t v = g->neighbours[e];
            if (!Blocked(s, u, v))
            {
                Relax(s, &size, v, s->cost[u] + g->lengths[e], u);
            }
        }
    }
    return last;
}

uint32_t maudSearchPath(maudPathSearch* search, const maudProbeGraph* graph,
                        const maudPathEnd* source, const maudPathEnd* listener,
                        maudVector3 sourcePoint, maudVector3 listenerPoint, maudVector3* vertices,
                        uint32_t* probes)
{
    uint32_t last = Run(search, graph, source, listener);
    uint32_t count = 0;
    for (uint32_t p = last; p != NONE && count <= MAUD_PATH_VERTICES; p = search->parent[p])
    {
        count += 1;
    }
    if (last == NONE || count + 2 > MAUD_PATH_VERTICES)
    {
        Reset(search);
        return 0;
    }
    vertices[0] = listenerPoint;
    uint32_t k = 1;
    for (uint32_t p = last; p != NONE; p = search->parent[p])
    {
        vertices[k] = graph->points[p];
        probes[k - 1] = p;
        k += 1;
    }
    vertices[k] = sourcePoint;
    Reset(search);
    return k + 1;
}
