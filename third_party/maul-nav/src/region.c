// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Partitioning a tile's open space into regions by layers.

#include "region.h"

#include "allocator.h"
#include "compact.h"
#include "sort.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// A sweep reaching more than one region below.
#define MANY 0xFFFFFFFFu

// The scratch the sweeps of one row need, each array sized for the row
// with the most spans.
typedef struct Sweeps
{
    uint32_t* below;
    uint32_t* samples;
    uint32_t* id;
    uint32_t* touched;
    // Per region: how many spans of the row reach it from above.
    uint32_t* reach;
} Sweeps;

// The region graph: for each region its spans, area, whether it touches
// the border, its layer, and the sorted lists of regions it links to and
// overlaps.
typedef struct Graph
{
    uint32_t count;
    uint32_t* spans;
    mnavAreaType* areas;
    uint8_t* border;
    uint32_t* layer;
    uint32_t* linkStart;
    uint64_t* links;
    uint32_t* overlapStart;
    uint64_t* overlaps;
} Graph;

static bool IsRegion(uint32_t id)
{
    return id != 0 && (id & MNAV_BORDER_REGION) == 0;
}

// The border side of a column, 1 to 4, or 0 inside. The -Z and +Z strips
// take the corners.
static uint32_t BorderSide(int32_t x, int32_t z, int32_t width, int32_t border)
{
    if (z < border)
    {
        return 3;
    }
    if (z >= width - border)
    {
        return 4;
    }
    if (x < border)
    {
        return 1;
    }
    return x >= width - border ? 2 : 0;
}

static void PaintBorder(const mnavCompactField* field, int32_t border, uint32_t* ids)
{
    int32_t width = field->frame.width;
    for (int32_t c = 0; c < width * width; ++c)
    {
        uint32_t side = BorderSide(c % width, c / width, width, border);
        for (uint32_t i = field->columns[c]; side != 0 && i < field->columns[c + 1]; ++i)
        {
            ids[i] = field->areas[i] != mnav_areaNone ? (MNAV_BORDER_REGION | side) : 0;
        }
    }
}

// The region of span i's linked neighbor in a direction when it has the
// same area and is not a border region, or 0.
static uint32_t SameAreaNeighbor(const mnavCompactField* field, const uint32_t* ids, int32_t x,
                                 int32_t z, uint32_t i, int32_t direction)
{
    if (field->spans[i].links[direction] == MNAV_NO_LINK)
    {
        return 0;
    }
    uint32_t n = mnavLinkedSpan(field, x, z, i, direction);
    return IsRegion(ids[n]) && field->areas[n] == field->areas[i] ? ids[n] : 0;
}

// Assigns the row's spans to sweeps, numbered from 1 in ids, and counts
// what each sweep reaches below. Returns the number of sweeps plus one.
static uint32_t CollectRow(const mnavCompactField* field, int32_t z, int32_t border, uint32_t* ids,
                           Sweeps* sweeps, uint32_t* touchedCount)
{
    int32_t width = field->frame.width;
    uint32_t next = 1;
    for (int32_t x = border; x < width - border; ++x)
    {
        int32_t c = x + z * width;
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            if (field->areas[i] == mnav_areaNone)
            {
                continue;
            }
            uint32_t sweep = SameAreaNeighbor(field, ids, x, z, i, 0);
            if (sweep == 0)
            {
                sweep = next++;
                sweeps->below[sweep] = 0;
                sweeps->samples[sweep] = 0;
            }
            uint32_t below = SameAreaNeighbor(field, ids, x, z, i, 3);
            if (below != 0)
            {
                if (sweeps->below[sweep] == 0 || sweeps->below[sweep] == below)
                {
                    sweeps->below[sweep] = below;
                    sweeps->samples[sweep] += 1;
                    if (sweeps->reach[below]++ == 0)
                    {
                        sweeps->touched[(*touchedCount)++] = below;
                    }
                }
                else
                {
                    sweeps->below[sweep] = MANY;
                }
            }
            ids[i] = sweep;
        }
    }
    return next;
}

// Partitions the rows inside the border into monotone regions. Returns
// the number of regions plus one.
static uint32_t SweepRows(const mnavCompactField* field, int32_t border, uint32_t* ids,
                          Sweeps* sweeps)
{
    int32_t width = field->frame.width;
    uint32_t nextRegion = 1;
    for (int32_t z = border; z < width - border; ++z)
    {
        uint32_t touched = 0;
        uint32_t count = CollectRow(field, z, border, ids, sweeps, &touched);
        // A sweep continues the region below when it is the only way the
        // row reaches that region.
        for (uint32_t s = 1; s < count; ++s)
        {
            uint32_t below = sweeps->below[s];
            bool continues =
                below != 0 && below != MANY && sweeps->reach[below] == sweeps->samples[s];
            sweeps->id[s] = continues ? below : nextRegion++;
        }
        for (uint32_t t = 0; t < touched; ++t)
        {
            sweeps->reach[sweeps->touched[t]] = 0;
        }
        uint32_t first = field->columns[border + z * width];
        uint32_t end = field->columns[width - border + z * width];
        for (uint32_t i = first; i < end; ++i)
        {
            if (IsRegion(ids[i]) && field->areas[i] != mnav_areaNone)
            {
                ids[i] = sweeps->id[ids[i]];
            }
        }
    }
    return nextRegion;
}

// Counts a pair, and writes it when keys is not NULL.
static void Emit(uint64_t* keys, size_t* count, uint32_t a, uint32_t b)
{
    if (keys != nullptr)
    {
        keys[*count] = ((uint64_t)a << 32) | b;
    }
    *count += 1;
}

// The pairs of regions that share a column, both ways round.
static size_t OverlapPairs(const mnavCompactField* field, const uint32_t* ids, uint64_t* keys)
{
    int32_t width = field->frame.width;
    size_t count = 0;
    for (int32_t c = 0; c < width * width; ++c)
    {
        uint32_t end = field->columns[c + 1];
        for (uint32_t i = field->columns[c]; i < end; ++i)
        {
            for (uint32_t k = i + 1; k < end; ++k)
            {
                if (IsRegion(ids[i]) && IsRegion(ids[k]) && ids[i] != ids[k])
                {
                    Emit(keys, &count, ids[i], ids[k]);
                    Emit(keys, &count, ids[k], ids[i]);
                }
            }
        }
    }
    return count;
}

// The pairs of linked regions, from each side; marks the regions linked
// to a border region.
static size_t LinkPairs(const mnavCompactField* field, const uint32_t* ids, Graph* graph,
                        uint64_t* keys)
{
    int32_t width = field->frame.width;
    size_t count = 0;
    for (int32_t c = 0; c < width * width; ++c)
    {
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            for (int32_t direction = 0; IsRegion(ids[i]) && direction < 4; ++direction)
            {
                if (field->spans[i].links[direction] == MNAV_NO_LINK)
                {
                    continue;
                }
                uint32_t b = ids[mnavLinkedSpan(field, c % width, c / width, i, direction)];
                if ((b & MNAV_BORDER_REGION) != 0)
                {
                    graph->border[ids[i]] = 1;
                }
                else if (b != 0 && b != ids[i])
                {
                    Emit(keys, &count, ids[i], b);
                }
            }
        }
    }
    return count;
}

// Visits every pair the graph needs: linked regions, or regions sharing a
// column when overlap is true. Each pair is counted when keys is NULL and
// written otherwise; returns the count.
static size_t Pairs(const mnavCompactField* field, const uint32_t* ids, Graph* graph, bool overlap,
                    uint64_t* keys)
{
    return overlap ? OverlapPairs(field, ids, keys) : LinkPairs(field, ids, graph, keys);
}

// Builds one sorted, distinct pair list and its start per region.
static mnavResult BuildPairs(mnavMemory* memory, const mnavCompactField* field, const uint32_t* ids,
                             Graph* graph, bool overlap, uint64_t** keysOut, uint32_t* starts,
                             size_t* countOut)
{
    size_t count = Pairs(field, ids, graph, overlap, nullptr);
    uint64_t* keys = nullptr;
    uint64_t* scratch = nullptr;
    mnavResult result =
        mnavAllocate(memory, count, sizeof(uint64_t), alignof(uint64_t), (void**)&keys);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, count, sizeof(uint64_t), alignof(uint64_t), (void**)&scratch);
    }
    if (result == mnav_success)
    {
        Pairs(field, ids, graph, overlap, keys);
        size_t unique = mnavSortUnique(keys, scratch, count);
        memset(starts, 0, ((size_t)graph->count + 1) * sizeof(uint32_t));
        for (size_t k = 0; k < unique; ++k)
        {
            starts[(keys[k] >> 32) + 1] += 1;
        }
        for (uint32_t r = 0; r < graph->count; ++r)
        {
            starts[r + 1] += starts[r];
        }
    }
    mnavRelease(memory, scratch, count, sizeof(uint64_t), alignof(uint64_t));
    *keysOut = keys;
    *countOut = count;
    return result;
}

// Puts region r in a layer and marks every region sharing a column with
// it, so none of those can join the same layer.
static void Take(Graph* graph, uint32_t r, uint32_t layer, uint32_t* marks, uint32_t* queue,
                 uint32_t* tail)
{
    graph->layer[r] = layer;
    queue[(*tail)++] = r;
    for (uint32_t k = graph->overlapStart[r]; k < graph->overlapStart[r + 1]; ++k)
    {
        marks[(uint32_t)graph->overlaps[k]] = layer;
    }
}

// Merges linked regions of one area into layers, breadth first from the
// lowest region, never taking a region that shares a column with one
// already in the layer. Returns the number of layers plus one.
static uint32_t MergeLayers(Graph* graph, uint32_t* queue, uint32_t* marks, uint32_t* layerSpans,
                            uint8_t* layerBorder)
{
    uint32_t layers = 1;
    for (uint32_t root = 1; root < graph->count; ++root)
    {
        if (graph->layer[root] != 0)
        {
            continue;
        }
        uint32_t layer = layers++;
        uint32_t head = 0;
        uint32_t tail = 0;
        layerSpans[layer] = 0;
        layerBorder[layer] = 0;
        Take(graph, root, layer, marks, queue, &tail);
        while (head < tail)
        {
            uint32_t r = queue[head++];
            layerSpans[layer] += graph->spans[r];
            layerBorder[layer] |= graph->border[r];
            for (uint32_t k = graph->linkStart[r]; k < graph->linkStart[r + 1]; ++k)
            {
                uint32_t n = (uint32_t)graph->links[k];
                if (graph->layer[n] == 0 && graph->areas[n] == graph->areas[r] && marks[n] != layer)
                {
                    Take(graph, n, layer, marks, queue, &tail);
                }
            }
        }
    }
    return layers;
}

// Drops small layers off the border, counting them, numbers the rest in
// layer order and writes each span's final region. Returns the number of
// regions.
static uint32_t Renumber(const Graph* graph, uint32_t layers, const uint32_t* layerSpans,
                         const uint8_t* layerBorder, int32_t minRegion, uint32_t* final,
                         uint32_t* ids, int32_t spanCount, uint32_t* dropped)
{
    uint32_t count = 0;
    for (uint32_t layer = 1; layer < layers; ++layer)
    {
        bool keep = layerBorder[layer] != 0 || layerSpans[layer] >= (uint32_t)minRegion;
        final[layer] = keep ? ++count : 0;
        *dropped += keep ? 0 : 1;
    }
    for (int32_t i = 0; i < spanCount; ++i)
    {
        if (IsRegion(ids[i]))
        {
            ids[i] = final[graph->layer[ids[i]]];
        }
    }
    return count;
}

// The arrays the build needs, in one list so that one loop allocates and
// one releases them.
typedef struct Block
{
    void** pointer;
    size_t count;
    size_t size;
    size_t alignment;
} Block;

static mnavResult AllocateBlocks(mnavMemory* memory, Block* blocks, int32_t count)
{
    for (int32_t b = 0; b < count; ++b)
    {
        mnavResult result = mnavAllocate(memory, blocks[b].count, blocks[b].size,
                                         blocks[b].alignment, blocks[b].pointer);
        if (result != mnav_success)
        {
            return result;
        }
        if (blocks[b].count > 0)
        {
            memset(*blocks[b].pointer, 0, blocks[b].count * blocks[b].size);
        }
    }
    return mnav_success;
}

static void ReleaseBlocks(mnavMemory* memory, Block* blocks, int32_t count)
{
    for (int32_t b = count - 1; b >= 0; --b)
    {
        mnavRelease(memory, *blocks[b].pointer, blocks[b].count, blocks[b].size,
                    blocks[b].alignment);
        *blocks[b].pointer = nullptr;
    }
}

static uint32_t WidestRow(const mnavCompactField* field)
{
    int32_t width = field->frame.width;
    uint32_t widest = 0;
    for (int32_t z = 0; z < width; ++z)
    {
        uint32_t spans = field->columns[(z + 1) * width] - field->columns[z * width];
        widest = spans > widest ? spans : widest;
    }
    return widest;
}

static mnavResult Sweep(mnavMemory* memory, const mnavCompactField* field, int32_t border,
                        uint32_t* ids, uint32_t* regionsOut)
{
    // A row has at most as many sweeps as spans, and the whole tile at
    // most as many regions as spans.
    size_t rowSize = (size_t)WidestRow(field) + 1;
    size_t regionSize = (size_t)field->spanCount + 1;
    Sweeps sweeps = {0};
    Block blocks[] = {
        {(void**)&sweeps.below, rowSize, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&sweeps.samples, rowSize, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&sweeps.id, rowSize, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&sweeps.touched, rowSize, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&sweeps.reach, regionSize, sizeof(uint32_t), alignof(uint32_t)},
    };
    int32_t blockCount = (int32_t)(sizeof(blocks) / sizeof(blocks[0]));
    mnavResult result = AllocateBlocks(memory, blocks, blockCount);
    if (result == mnav_success)
    {
        PaintBorder(field, border, ids);
        *regionsOut = SweepRows(field, border, ids, &sweeps);
    }
    ReleaseBlocks(memory, blocks, blockCount);
    return result;
}

static mnavResult Layer(mnavMemory* memory, const mnavCompactField* field, uint32_t regions,
                        int32_t minRegion, mnavRegionMap* map)
{
    Graph graph = {regions, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint32_t* queue = nullptr;
    uint32_t* marks = nullptr;
    uint32_t* layerSpans = nullptr;
    uint8_t* layerBorder = nullptr;
    uint32_t* final = nullptr;
    size_t size = regions + 1;
    Block blocks[] = {
        {(void**)&graph.spans, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&graph.areas, size, sizeof(mnavAreaType), alignof(mnavAreaType)},
        {(void**)&graph.border, size, sizeof(uint8_t), alignof(uint8_t)},
        {(void**)&graph.layer, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&graph.linkStart, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&graph.overlapStart, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&queue, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&marks, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&layerSpans, size, sizeof(uint32_t), alignof(uint32_t)},
        {(void**)&layerBorder, size, sizeof(uint8_t), alignof(uint8_t)},
        {(void**)&final, size, sizeof(uint32_t), alignof(uint32_t)},
    };
    int32_t blockCount = (int32_t)(sizeof(blocks) / sizeof(blocks[0]));
    mnavResult result = AllocateBlocks(memory, blocks, blockCount);
    size_t linkCount = 0;
    size_t overlapCount = 0;
    if (result == mnav_success)
    {
        for (int32_t i = 0; i < field->spanCount; ++i)
        {
            if (IsRegion(map->ids[i]))
            {
                graph.spans[map->ids[i]] += 1;
                graph.areas[map->ids[i]] = field->areas[i];
            }
        }
        result = BuildPairs(memory, field, map->ids, &graph, false, &graph.links, graph.linkStart,
                            &linkCount);
    }
    if (result == mnav_success)
    {
        result = BuildPairs(memory, field, map->ids, &graph, true, &graph.overlaps,
                            graph.overlapStart, &overlapCount);
    }
    if (result == mnav_success)
    {
        uint32_t layers = MergeLayers(&graph, queue, marks, layerSpans, layerBorder);
        map->count = Renumber(&graph, layers, layerSpans, layerBorder, minRegion, final, map->ids,
                              map->spanCount, &map->dropped);
    }
    mnavRelease(memory, graph.overlaps, overlapCount, sizeof(uint64_t), alignof(uint64_t));
    mnavRelease(memory, graph.links, linkCount, sizeof(uint64_t), alignof(uint64_t));
    ReleaseBlocks(memory, blocks, blockCount);
    return result;
}

mnavResult mnavBuildRegions(mnavMemory* memory, const mnavCompactField* field, int32_t border,
                            int32_t minRegion, mnavRegionMap* map)
{
    *map = (mnavRegionMap){nullptr, field->spanCount, 0, 0};
    mnavResult result = mnavAllocate(memory, (size_t)field->spanCount, sizeof(uint32_t),
                                     alignof(uint32_t), (void**)&map->ids);
    if (result != mnav_success)
    {
        return result;
    }
    if (field->spanCount > 0)
    {
        memset(map->ids, 0, (size_t)field->spanCount * sizeof(uint32_t));
    }
    uint32_t regions = 0;
    result = Sweep(memory, field, border, map->ids, &regions);
    if (result == mnav_success)
    {
        result = Layer(memory, field, regions, minRegion, map);
    }
    if (result != mnav_success)
    {
        mnavReleaseRegions(memory, map);
    }
    return result;
}

void mnavReleaseRegions(mnavMemory* memory, mnavRegionMap* map)
{
    mnavRelease(memory, map->ids, (size_t)map->spanCount, sizeof(uint32_t), alignof(uint32_t));
    map->ids = nullptr;
    map->count = 0;
}
