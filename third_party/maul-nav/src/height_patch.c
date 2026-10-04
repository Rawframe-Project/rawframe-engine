// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The floor heights under one polygon.

#include "height_patch.h"

#include "allocator.h"
#include "compact.h"
#include "polymesh.h"
#include "region.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// The flood's state: the field, the patch and the queue's ends.
typedef struct Flood
{
    const mnavCompactField* field;
    int32_t border;
    mnavHeightPatch* patch;
    int32_t head;
    int32_t tail;
} Flood;

// The patch index of field column (fx, fz), or -1 outside the patch.
static int32_t PatchIndex(const Flood* flood, int32_t fx, int32_t fz)
{
    const mnavHeightPatch* patch = flood->patch;
    int32_t x = fx - flood->border - patch->minX;
    int32_t z = fz - flood->border - patch->minZ;
    if (x < 0 || z < 0 || x >= patch->width || z >= patch->depth)
    {
        return -1;
    }
    return x + z * patch->width;
}

// Sets a cell's height from span i of column (fx, fz) and queues the span.
static void Take(Flood* flood, int32_t fx, int32_t fz, uint32_t i, int32_t index)
{
    flood->patch->heights[index] = flood->field->spans[i].floor;
    flood->patch->queue[flood->tail * 2 + 0] = i;
    flood->patch->queue[flood->tail * 2 + 1] = (uint32_t)(fx + fz * flood->field->frame.width);
    flood->tail += 1;
}

// Takes each cell's span of the region; returns whether any cell had one.
// Only spans at the region's edge are queued, as only they can reach
// cells the region does not cover.
static bool TakeRegion(Flood* flood, const uint32_t* ids, uint32_t region)
{
    const mnavCompactField* field = flood->field;
    const mnavHeightPatch* patch = flood->patch;
    bool any = false;
    for (int32_t z = 0; z < patch->depth; ++z)
    {
        for (int32_t x = 0; x < patch->width; ++x)
        {
            int32_t fx = patch->minX + x + flood->border;
            int32_t fz = patch->minZ + z + flood->border;
            int32_t column = fx + fz * field->frame.width;
            for (uint32_t i = field->columns[column]; i < field->columns[column + 1]; ++i)
            {
                if (ids[i] != region)
                {
                    continue;
                }
                any = true;
                bool edge = false;
                for (int32_t d = 0; d < 4 && !edge; ++d)
                {
                    edge = field->spans[i].links[d] != MNAV_NO_LINK &&
                           ids[mnavLinkedSpan(field, fx, fz, i, d)] != region;
                }
                if (edge)
                {
                    Take(flood, fx, fz, i, x + z * patch->width);
                }
                else
                {
                    flood->patch->heights[x + z * patch->width] = field->spans[i].floor;
                }
                break;
            }
        }
    }
    return any;
}

// Seeds from the walkable span nearest each vertex's height among the up
// to four cells round the vertex's corner, the first in cell and span
// order on ties.
static void TakeVertices(Flood* flood, const mnavPolyMesh* mesh, const mnavPolygon* polygon)
{
    const mnavCompactField* field = flood->field;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        const mnavMeshVertex* v = &mesh->vertices[polygon->vertices[k]];
        int32_t best = -1;
        int32_t bestIndex = -1;
        int32_t bestColumn = 0;
        int32_t bestDistance = 0;
        for (int32_t c = 0; c < 4; ++c)
        {
            int32_t fx = v->x - 1 + (c & 1) + flood->border;
            int32_t fz = v->z - 1 + (c >> 1) + flood->border;
            int32_t index = PatchIndex(flood, fx, fz);
            if (index < 0 || fx >= field->frame.width || fz >= field->frame.width)
            {
                continue;
            }
            int32_t column = fx + fz * field->frame.width;
            for (uint32_t i = field->columns[column]; i < field->columns[column + 1]; ++i)
            {
                int32_t distance = field->spans[i].floor - v->y;
                distance = distance < 0 ? -distance : distance;
                if (field->areas[i] != mnav_areaNone && (best < 0 || distance < bestDistance))
                {
                    best = (int32_t)i;
                    bestIndex = index;
                    bestColumn = column;
                    bestDistance = distance;
                }
            }
        }
        if (best >= 0 && flood->patch->heights[bestIndex] == MNAV_UNSET_HEIGHT)
        {
            int32_t width = field->frame.width;
            Take(flood, bestColumn % width, bestColumn / width, (uint32_t)best, bestIndex);
        }
    }
}

// Floods from the queued spans across links into unset cells, first in
// first out, directions in order.
static void Spread(Flood* flood)
{
    const mnavCompactField* field = flood->field;
    int32_t width = field->frame.width;
    while (flood->head < flood->tail)
    {
        uint32_t i = flood->patch->queue[flood->head * 2 + 0];
        int32_t column = (int32_t)flood->patch->queue[flood->head * 2 + 1];
        flood->head += 1;
        int32_t fx = column % width;
        int32_t fz = column / width;
        for (int32_t d = 0; d < 4; ++d)
        {
            if (field->spans[i].links[d] == MNAV_NO_LINK)
            {
                continue;
            }
            int32_t nx = fx + mnavDirectionX(d);
            int32_t nz = fz + mnavDirectionZ(d);
            int32_t index = PatchIndex(flood, nx, nz);
            if (index >= 0 && flood->patch->heights[index] == MNAV_UNSET_HEIGHT)
            {
                Take(flood, nx, nz, mnavLinkedSpan(field, fx, fz, i, d), index);
            }
        }
    }
}

// Sizes the patch to the polygon's cells and one more on each side,
// within the field, and clears it.
static mnavResult Size(mnavMemory* memory, const mnavCompactField* field, int32_t border,
                       const mnavPolyMesh* mesh, const mnavPolygon* polygon, mnavHeightPatch* patch)
{
    int32_t minX = INT32_MAX;
    int32_t minZ = INT32_MAX;
    int32_t maxX = INT32_MIN;
    int32_t maxZ = INT32_MIN;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        const mnavMeshVertex* v = &mesh->vertices[polygon->vertices[k]];
        minX = v->x < minX ? v->x : minX;
        minZ = v->z < minZ ? v->z : minZ;
        maxX = v->x > maxX ? v->x : maxX;
        maxZ = v->z > maxZ ? v->z : maxZ;
    }
    int32_t low = -border;
    int32_t high = field->frame.width - border;
    patch->minX = minX - 1 < low ? low : minX - 1;
    patch->minZ = minZ - 1 < low ? low : minZ - 1;
    patch->width = (maxX + 1 > high ? high : maxX + 1) - patch->minX;
    patch->depth = (maxZ + 1 > high ? high : maxZ + 1) - patch->minZ;
    int32_t cells = patch->width * patch->depth;
    mnavResult result = mnavReserve(memory, (void**)&patch->heights, &patch->capacity, 0, cells,
                                    sizeof(uint16_t), alignof(uint16_t));
    if (result == mnav_success)
    {
        result = mnavReserve(memory, (void**)&patch->queue, &patch->queueCapacity, 0, cells * 2,
                             sizeof(uint32_t), alignof(uint32_t));
    }
    if (result == mnav_success)
    {
        memset(patch->heights, 0xFF, (size_t)cells * sizeof(uint16_t));
    }
    return result;
}

mnavResult mnavFillHeightPatch(mnavMemory* memory, const mnavCompactField* field,
                               const mnavRegionMap* regions, int32_t border,
                               const mnavPolyMesh* mesh, int32_t polygon, mnavHeightPatch* patch)
{
    const mnavPolygon* p = &mesh->polygons[polygon];
    mnavResult result = Size(memory, field, border, mesh, p, patch);
    if (result != mnav_success)
    {
        return result;
    }
    Flood flood = {field, border, patch, 0, 0};
    bool taken = p->region != MNAV_MIXED_REGION && TakeRegion(&flood, regions->ids, p->region);
    if (!taken)
    {
        TakeVertices(&flood, mesh, p);
    }
    Spread(&flood);
    return mnav_success;
}

// The set height nearest reference among the cells of the ring at
// distance r round (cx, z), the first in ring order on ties, or
// MNAV_UNSET_HEIGHT. The ring's cells: whole rows at its top and bottom,
// the two ends of the rows between.
static uint16_t SearchRing(const mnavHeightPatch* patch, int32_t cx, int32_t cz, int32_t r,
                           int32_t reference)
{
    uint16_t found = MNAV_UNSET_HEIGHT;
    int32_t best = -1;
    for (int32_t dz = -r; dz <= r; ++dz)
    {
        int32_t step = dz == -r || dz == r ? 1 : 2 * r;
        int32_t pz = cz + dz;
        for (int32_t dx = -r; dx <= r && pz >= 0 && pz < patch->depth; dx += step)
        {
            int32_t px = cx + dx;
            uint16_t h = px >= 0 && px < patch->width ? patch->heights[px + pz * patch->width]
                                                      : (uint16_t)MNAV_UNSET_HEIGHT;
            int32_t distance = h > reference ? h - reference : reference - h;
            if (h != MNAV_UNSET_HEIGHT && (best < 0 || distance < best))
            {
                best = distance;
                found = h;
            }
        }
    }
    return found;
}

bool mnavPatchHeight(const mnavHeightPatch* patch, int32_t x, int32_t z, int32_t reference,
                     int32_t radius, uint16_t* height)
{
    int32_t cx = x - patch->minX;
    int32_t cz = z - patch->minZ;
    cx = cx < 0 ? 0 : (cx >= patch->width ? patch->width - 1 : cx);
    cz = cz < 0 ? 0 : (cz >= patch->depth ? patch->depth - 1 : cz);
    uint16_t found = patch->heights[cx + cz * patch->width];
    for (int32_t r = 1; r <= radius && found == MNAV_UNSET_HEIGHT; ++r)
    {
        found = SearchRing(patch, cx, cz, r, reference);
    }
    *height = found;
    return found != MNAV_UNSET_HEIGHT;
}

void mnavReleaseHeightPatch(mnavMemory* memory, mnavHeightPatch* patch)
{
    mnavRelease(memory, patch->queue, (size_t)patch->queueCapacity, sizeof(uint32_t),
                alignof(uint32_t));
    mnavRelease(memory, patch->heights, (size_t)patch->capacity, sizeof(uint16_t),
                alignof(uint16_t));
    *patch = (mnavHeightPatch){0};
}
