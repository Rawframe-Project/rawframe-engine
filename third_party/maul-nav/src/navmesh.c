// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh: staged tiles, atomic commits and links across tile sides.

#include "navmesh.h"

#include "allocator.h"
#include "bake_def.h"
#include "detail.h"
#include "polymesh.h"
#include "tile.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

mnavBakeDefResult mnavCreateNavmesh(const mnavBakeDef* def, mnavNavmesh** navmeshOut)
{
    if (navmeshOut == nullptr)
    {
        return (mnavBakeDefResult){mnav_errorInvalid, mnav_settingNone};
    }
    *navmeshOut = nullptr;
    mnavBakeCells cells = {0};
    mnavBakeDefResult checked = mnavCheckBakeDef(def, &cells);
    if (checked.result != mnav_success)
    {
        return checked;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavNavmesh* navmesh = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavNavmesh), alignof(mnavNavmesh), (void**)&navmesh);
    if (result != mnav_success)
    {
        return (mnavBakeDefResult){result, mnav_settingNone};
    }
    *navmesh = (mnavNavmesh){0};
    for (int32_t k = 0; k < MNAV_LINK_KINDS; ++k)
    {
        navmesh->costPerMeter[k] = (double)INFINITY;
    }
    navmesh->def = *def;
    navmesh->cells = cells;
    navmesh->memory = memory;
    *navmeshOut = navmesh;
    return (mnavBakeDefResult){mnav_success, mnav_settingNone};
}

static void ReleaseLinks(mnavMemory* memory, mnavTile* tile)
{
    mnavRelease(memory, tile->links, (size_t)tile->linkCount, sizeof(mnavLink), alignof(mnavLink));
    mnavRelease(memory, tile->firstLink, (size_t)tile->mesh.polygonCount + 1, sizeof(int32_t),
                alignof(int32_t));
    tile->links = nullptr;
    tile->linkCount = 0;
    tile->firstLink = nullptr;
}

static void FreeTile(mnavMemory* memory, mnavTile* tile)
{
    if (tile == nullptr)
    {
        return;
    }
    ReleaseLinks(memory, tile);
    mnavReleaseDetailMesh(memory, &tile->detail);
    mnavReleasePolyMesh(memory, &tile->mesh);
    mnavRelease(memory, tile, 1, sizeof(mnavTile), alignof(mnavTile));
}

void mnavDestroyNavmesh(mnavNavmesh* navmesh)
{
    if (navmesh == nullptr)
    {
        return;
    }
    mnavMemory* memory = &navmesh->memory;
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        FreeTile(memory, navmesh->staged[s].tile);
    }
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        FreeTile(memory, navmesh->slots[s].tile);
    }
    mnavRelease(memory, navmesh->staged, (size_t)navmesh->stagedCapacity, sizeof(mnavStaged),
                alignof(mnavStaged));
    mnavRelease(memory, navmesh->links, (size_t)navmesh->linkCapacity, sizeof(mnavOffLink),
                alignof(mnavOffLink));
    mnavRelease(memory, navmesh->areaChanges, (size_t)navmesh->areaChangeCapacity,
                sizeof(mnavAreaChange), alignof(mnavAreaChange));
    mnavRelease(memory, navmesh->attachments, (size_t)navmesh->attachmentCapacity, sizeof(uint64_t),
                alignof(uint64_t));
    mnavRelease(memory, navmesh->places, (size_t)navmesh->placeCapacity, sizeof(mnavPlace),
                alignof(mnavPlace));
    mnavRelease(memory, navmesh->slots, (size_t)navmesh->slotCapacity, sizeof(mnavSlot),
                alignof(mnavSlot));
    mnavMemory last = *memory;
    mnavRelease(&last, navmesh, 1, sizeof(mnavNavmesh), alignof(mnavNavmesh));
}

static bool Before(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    return ax != bx ? ax < bx : az < bz;
}

// The index of place (x, z) in sorted places, or -1.
static int32_t FindPlace(const mnavPlace* places, int32_t count, int32_t x, int32_t z)
{
    int32_t low = 0;
    int32_t high = count;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        if (Before(places[middle].x, places[middle].z, x, z))
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < count && places[low].x == x && places[low].z == z ? low : -1;
}

const mnavTile* mnavTileAt(const mnavNavmesh* navmesh, int32_t x, int32_t z, int32_t* slotOut)
{
    int32_t at = FindPlace(navmesh->places, navmesh->placeCount, x, z);
    *slotOut = at >= 0 ? navmesh->places[at].slot : -1;
    return at >= 0 ? navmesh->slots[*slotOut].tile : nullptr;
}

const mnavPolygon* mnavPolygonOf(const mnavNavmesh* navmesh, mnavPolygonId id,
                                 const mnavTile** tileOut)
{
    *tileOut = nullptr;
    if (id.slot == 0 || id.slot > (uint32_t)navmesh->slotCount)
    {
        return nullptr;
    }
    const mnavSlot* slot = &navmesh->slots[id.slot - 1];
    if (slot->tile == nullptr || slot->generation != id.generation ||
        id.polygon >= (uint32_t)slot->tile->mesh.polygonCount)
    {
        return nullptr;
    }
    *tileOut = slot->tile;
    return &slot->tile->mesh.polygons[id.polygon];
}

mnavResult mnavGetTile(const mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ,
                       mnavTileId* tileOut)
{
    if (navmesh == nullptr || tileOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    int32_t slot = -1;
    const mnavTile* tile = mnavTileAt(navmesh, tileX, tileZ, &slot);
    *tileOut = tile != nullptr ? (mnavTileId){(uint32_t)slot + 1, navmesh->slots[slot].generation}
                               : (mnavTileId){0, 0};
    return tile != nullptr ? mnav_success : mnav_errorNotLoaded;
}

static uint32_t FloatBits(float f)
{
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static uint64_t DoubleBits(double d)
{
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Whether a tile was baked with the navmesh's settings, bit for bit.
static bool Matches(const mnavNavmesh* navmesh, const mnavTileInfo* info)
{
    const mnavBakeDef* def = &navmesh->def;
    const mnavBakeCells* cells = &navmesh->cells;
    return info->tileCells == def->tileCells &&
           FloatBits(info->cellSize) == FloatBits(def->cellSize) &&
           FloatBits(info->cellHeight) == FloatBits(def->cellHeight) &&
           DoubleBits(info->origin.x) == DoubleBits(def->origin.x) &&
           DoubleBits(info->origin.y) == DoubleBits(def->origin.y) &&
           DoubleBits(info->origin.z) == DoubleBits(def->origin.z) &&
           info->agentHeight == cells->agentHeight && info->agentRadius == cells->agentRadius &&
           info->agentStep == cells->agentStep;
}

// Stages a tile, or a removal for NULL, at a place, replacing what was
// staged there.
static mnavResult Stage(mnavNavmesh* navmesh, int32_t x, int32_t z, mnavTile* tile)
{
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        mnavStaged* staged = &navmesh->staged[s];
        if (staged->x == x && staged->z == z)
        {
            FreeTile(&navmesh->memory, staged->tile);
            staged->tile = tile;
            return mnav_success;
        }
    }
    mnavResult result = mnavReserve(
        &navmesh->memory, (void**)&navmesh->staged, &navmesh->stagedCapacity, navmesh->stagedCount,
        navmesh->stagedCount + 1, sizeof(mnavStaged), alignof(mnavStaged));
    if (result == mnav_success)
    {
        navmesh->staged[navmesh->stagedCount++] = (mnavStaged){x, z, tile};
    }
    return result;
}

mnavTileResult mnavStageTile(mnavNavmesh* navmesh, const uint8_t* bytes, size_t size)
{
    if (navmesh == nullptr)
    {
        return (mnavTileResult){mnav_errorInvalid, mnav_tileHeader, -1};
    }
    mnavMemory* memory = &navmesh->memory;
    mnavTile* tile = nullptr;
    mnavResult allocated =
        mnavAllocate(memory, 1, sizeof(mnavTile), alignof(mnavTile), (void**)&tile);
    if (allocated != mnav_success)
    {
        return (mnavTileResult){allocated, mnav_tileHeader, -1};
    }
    *tile = (mnavTile){0};
    mnavTileResult result =
        mnavDecodeTile(memory, bytes, size, &tile->info, &tile->mesh, &tile->detail);
    if (result.result == mnav_success && !Matches(navmesh, &tile->info))
    {
        result = (mnavTileResult){mnav_errorInvalid, mnav_tileHeader, -1};
    }
    int32_t held = 0;
    if (result.result == mnav_success && navmesh->def.tier < mnav_tierDynamic &&
        mnavTileAt(navmesh, tile->info.x, tile->info.z, &held) != nullptr)
    {
        result = (mnavTileResult){mnav_errorTier, mnav_tileHeader, -1};
    }
    if (result.result == mnav_success)
    {
        mnavResult staged = Stage(navmesh, tile->info.x, tile->info.z, tile);
        result = (mnavTileResult){staged, mnav_tileHeader, -1};
        tile = staged == mnav_success ? nullptr : tile;
    }
    FreeTile(memory, tile);
    return result;
}

mnavResult mnavStageTileRemoval(mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ)
{
    if (navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    return Stage(navmesh, tileX, tileZ, nullptr);
}

void mnavAcross(int32_t side, int32_t* x, int32_t* z, int32_t* facing)
{
    *x += side == 1 ? -1 : (side == 3 ? 1 : 0);
    *z += side == 4 ? -1 : (side == 2 ? 1 : 0);
    *facing = side <= 2 ? side + 2 : side - 2;
}

// An edge's ends along a tile side's axis and their heights.
typedef struct Span
{
    int64_t u0;
    int64_t u1;
    int64_t y0;
    int64_t y1;
} Span;

static Span SpanOf(const mnavPolyMesh* mesh, const mnavPolygon* polygon, int32_t k, int32_t side)
{
    const mnavMeshVertex* a = &mesh->vertices[polygon->vertices[k]];
    const mnavMeshVertex* b = &mesh->vertices[polygon->vertices[(k + 1) % polygon->count]];
    bool alongZ = side == 1 || side == 3;
    return (Span){alongZ ? a->z : a->x, alongZ ? b->z : b->x, a->y, b->y};
}

// Whether two edges' heights at u, each interpolated along its edge, lie
// within step of each other, compared exactly.
static bool CloseAt(const Span* a, const Span* b, int64_t u, int64_t step)
{
    int64_t da = a->u1 - a->u0;
    int64_t db = b->u1 - b->u0;
    int64_t na = a->y0 * (a->u1 - u) + a->y1 * (u - a->u0);
    int64_t nb = b->y0 * (b->u1 - u) + b->y1 * (u - b->u0);
    int64_t difference = na * db - nb * da;
    int64_t scale = da * db;
    difference = difference < 0 ? -difference : difference;
    scale = scale < 0 ? -scale : scale;
    return difference <= step * scale;
}

// The part of the side two facing edges share, with heights within the
// agent's step at both its ends; false when they share none.
static bool Overlap(const Span* a, const Span* b, int64_t step, int32_t* low, int32_t* high)
{
    int64_t lo = a->u0 < a->u1 ? a->u0 : a->u1;
    int64_t hi = a->u0 < a->u1 ? a->u1 : a->u0;
    int64_t blo = b->u0 < b->u1 ? b->u0 : b->u1;
    int64_t bhi = b->u0 < b->u1 ? b->u1 : b->u0;
    lo = lo > blo ? lo : blo;
    hi = hi < bhi ? hi : bhi;
    if (lo >= hi || !CloseAt(a, b, lo, step) || !CloseAt(a, b, hi, step))
    {
        return false;
    }
    *low = (int32_t)lo;
    *high = (int32_t)hi;
    return true;
}

// The slot of the tile at a place after the commit, or -1.
static int32_t PlannedSlot(const mnavTilePlan* plan, int32_t x, int32_t z)
{
    int32_t at = FindPlace(plan->places, plan->placeCount, x, z);
    return at >= 0 ? plan->places[at].slot : -1;
}

// One polygon edge on a tile side, and the tile across that side.
typedef struct Edge
{
    int32_t polygon;
    int32_t edge;
    int32_t side;
    int32_t facing;
    int32_t other;
    Span span;
} Edge;

// Visits the links from one edge to the polygons across its side: counts
// them into first, or with links writes each at its polygon's cursor.
static void VisitAcross(const mnavNavmesh* navmesh, const mnavTilePlan* plan, const Edge* from,
                        int32_t* first, mnavLink* links)
{
    const mnavPolyMesh* across = &plan->slots[from->other].tile->mesh;
    int64_t step = navmesh->cells.agentStep;
    int32_t p = from->polygon;
    for (int32_t q = 0; q < across->polygonCount; ++q)
    {
        const mnavPolygon* target = &across->polygons[q];
        for (int32_t j = 0; j < target->count; ++j)
        {
            int32_t low = 0;
            int32_t high = 0;
            if (target->sides[j] != from->facing)
            {
                continue;
            }
            Span b = SpanOf(across, target, j, from->side);
            if (!Overlap(&from->span, &b, step, &low, &high))
            {
                continue;
            }
            if (links == nullptr)
            {
                first[p + 1] += 1;
                continue;
            }
            mnavPolygonId id = {(uint32_t)from->other + 1, plan->slots[from->other].generation,
                                (uint32_t)q};
            links[first[p]++] =
                (mnavLink){(uint16_t)p, (uint8_t)from->edge, (uint8_t)from->side, low, high, id};
        }
    }
}

// Visits every link of polygon edges on slot's tile sides: with links
// NULL counts them per polygon into first; otherwise writes each at its
// polygon's cursor in first.
static void VisitLinks(const mnavNavmesh* navmesh, const mnavTilePlan* plan, int32_t slot,
                       int32_t* first, mnavLink* links)
{
    const mnavSlot* own = &plan->slots[slot];
    const mnavPolyMesh* mesh = &own->tile->mesh;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavPolygon* polygon = &mesh->polygons[p];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            Edge from = {p, k, polygon->sides[k], 0, -1, {0}};
            int32_t x = own->x;
            int32_t z = own->z;
            if (from.side == 0)
            {
                continue;
            }
            mnavAcross(from.side, &x, &z, &from.facing);
            from.other = PlannedSlot(plan, x, z);
            if (from.other >= 0)
            {
                from.span = SpanOf(mesh, polygon, k, from.side);
                VisitAcross(navmesh, plan, &from, first, links);
            }
        }
    }
}

// Builds the links of slot's tile after the commit, counting first.
static mnavResult BuildLinks(mnavNavmesh* navmesh, mnavTilePlan* plan, int32_t slot)
{
    mnavMemory* memory = &navmesh->memory;
    int32_t polygons = plan->slots[slot].tile->mesh.polygonCount;
    mnavRelink relink = {slot, nullptr, 0, nullptr};
    mnavResult result =
        mnavReserve(memory, (void**)&plan->relinks, &plan->relinkCapacity, plan->relinkCount,
                    plan->relinkCount + 1, sizeof(mnavRelink), alignof(mnavRelink));
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)polygons + 1, sizeof(int32_t), alignof(int32_t),
                              (void**)&relink.firstLink);
    }
    if (result != mnav_success)
    {
        return result;
    }
    memset(relink.firstLink, 0, ((size_t)polygons + 1) * sizeof(int32_t));
    VisitLinks(navmesh, plan, slot, relink.firstLink, nullptr);
    for (int32_t p = 0; p < polygons; ++p)
    {
        relink.firstLink[p + 1] += relink.firstLink[p];
    }
    relink.linkCount = relink.firstLink[polygons];
    result = relink.linkCount > navmesh->def.limits.tileLinks ? mnav_errorLimit : mnav_success;
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)relink.linkCount, sizeof(mnavLink), alignof(mnavLink),
                              (void**)&relink.links);
    }
    if (result == mnav_success)
    {
        // The pass writes each polygon's links at a cursor that ends where
        // the next polygon's begin; the cursors are moved back after.
        VisitLinks(navmesh, plan, slot, relink.firstLink, relink.links);
        for (int32_t p = polygons; p > 0; --p)
        {
            relink.firstLink[p] = relink.firstLink[p - 1];
        }
        relink.firstLink[0] = 0;
    }
    // The relink is recorded either way, so a failure releases it.
    relink.linkCount = relink.links != nullptr ? relink.linkCount : 0;
    plan->relinks[plan->relinkCount++] = relink;
    return result;
}

static void InsertionSortStaged(mnavStaged* staged, int32_t count)
{
    for (int32_t i = 1; i < count; ++i)
    {
        mnavStaged item = staged[i];
        int32_t j = i;
        while (j > 0 && Before(item.x, item.z, staged[j - 1].x, staged[j - 1].z))
        {
            staged[j] = staged[j - 1];
            j -= 1;
        }
        staged[j] = item;
    }
}

// A free slot for a new tile: the lowest free one, or a new one.
static int32_t TakeSlot(mnavTilePlan* plan)
{
    for (int32_t s = 0; s < plan->slotCount; ++s)
    {
        if (plan->slots[s].tile == nullptr && !plan->slots[s].retired)
        {
            return s;
        }
    }
    plan->slots[plan->slotCount] = (mnavSlot){0, false, 0, 0, nullptr};
    return plan->slotCount++;
}

// Puts a staged change into the planned slots: a replacement keeps its
// slot with the next generation, unless that would wrap, which retires
// the slot; a removal empties its slot with the next generation.
static void PlanChange(mnavTilePlan* plan, int32_t existing, const mnavStaged* staged)
{
    if (existing >= 0)
    {
        mnavSlot* slot = &plan->slots[existing];
        slot->tile = nullptr;
        slot->retired = slot->generation == UINT32_MAX;
        slot->generation += slot->retired ? 0 : 1;
        if (staged->tile != nullptr && !slot->retired)
        {
            slot->tile = staged->tile;
            return;
        }
    }
    if (staged->tile != nullptr)
    {
        mnavSlot* slot = &plan->slots[TakeSlot(plan)];
        slot->generation += 1;
        slot->x = staged->x;
        slot->z = staged->z;
        slot->tile = staged->tile;
    }
}

// Merges the committed places with the staged changes, both sorted, into
// the planned places.
static void PlanPlaces(const mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    int32_t a = 0;
    int32_t b = 0;
    plan->placeCount = 0;
    while (a < navmesh->placeCount || b < navmesh->stagedCount)
    {
        const mnavPlace* old = a < navmesh->placeCount ? &navmesh->places[a] : nullptr;
        const mnavStaged* change = b < navmesh->stagedCount ? &navmesh->staged[b] : nullptr;
        bool takeOld =
            change == nullptr || (old != nullptr && Before(old->x, old->z, change->x, change->z));
        if (takeOld)
        {
            plan->places[plan->placeCount++] = *old;
            a += 1;
            continue;
        }
        a += old != nullptr && old->x == change->x && old->z == change->z ? 1 : 0;
        b += 1;
        if (change->tile == nullptr)
        {
            continue;
        }
        for (int32_t s = 0; s < plan->slotCount; ++s)
        {
            if (plan->slots[s].tile == change->tile)
            {
                plan->places[plan->placeCount++] = (mnavPlace){change->x, change->z, s};
            }
        }
    }
}

static void ReleasePlan(mnavMemory* memory, mnavTilePlan* plan)
{
    for (int32_t r = 0; r < plan->relinkCount; ++r)
    {
        mnavRelink* relink = &plan->relinks[r];
        int32_t polygons = plan->slots[relink->slot].tile->mesh.polygonCount;
        mnavRelease(memory, relink->links, (size_t)relink->linkCount, sizeof(mnavLink),
                    alignof(mnavLink));
        mnavRelease(memory, relink->firstLink, (size_t)polygons + 1, sizeof(int32_t),
                    alignof(int32_t));
    }
    mnavRelease(memory, plan->relinks, (size_t)plan->relinkCapacity, sizeof(mnavRelink),
                alignof(mnavRelink));
    mnavRelease(memory, plan->places, (size_t)plan->placeCapacity, sizeof(mnavPlace),
                alignof(mnavPlace));
    mnavRelease(memory, plan->slots, (size_t)plan->slotCapacity, sizeof(mnavSlot),
                alignof(mnavSlot));
    *plan = (mnavTilePlan){0};
}

// Whether slot's tile lies next to, or at, a staged place.
static bool NearChange(const mnavNavmesh* navmesh, const mnavSlot* slot)
{
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        int32_t dx = slot->x - navmesh->staged[s].x;
        int32_t dz = slot->z - navmesh->staged[s].z;
        bool beside = (dx == 0 && (dz == -1 || dz == 1)) || (dz == 0 && (dx == -1 || dx == 1));
        if ((dx == 0 && dz == 0) || beside)
        {
            return true;
        }
    }
    return false;
}

// Builds the navmesh after the commit beside the one queries read.
static mnavResult MakePlan(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    mnavMemory* memory = &navmesh->memory;
    plan->slotCapacity = navmesh->slotCount + navmesh->stagedCount;
    plan->placeCapacity = navmesh->placeCount + navmesh->stagedCount;
    mnavResult result = mnavAllocate(memory, (size_t)plan->slotCapacity, sizeof(mnavSlot),
                                     alignof(mnavSlot), (void**)&plan->slots);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)plan->placeCapacity, sizeof(mnavPlace),
                              alignof(mnavPlace), (void**)&plan->places);
    }
    if (result != mnav_success)
    {
        return result;
    }
    plan->slotCount = navmesh->slotCount;
    if (navmesh->slotCount > 0)
    {
        memcpy(plan->slots, navmesh->slots, (size_t)navmesh->slotCount * sizeof(mnavSlot));
    }
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        int32_t existing = -1;
        (void)mnavTileAt(navmesh, navmesh->staged[s].x, navmesh->staged[s].z, &existing);
        PlanChange(plan, existing, &navmesh->staged[s]);
    }
    PlanPlaces(navmesh, plan);
    if (plan->placeCount > navmesh->def.limits.tiles)
    {
        return mnav_errorLimit;
    }
    for (int32_t s = 0; s < plan->slotCount && result == mnav_success; ++s)
    {
        if (plan->slots[s].tile != nullptr && NearChange(navmesh, &plan->slots[s]))
        {
            result = BuildLinks(navmesh, plan, s);
        }
    }
    return result;
}

// Swaps the plan in: frees the tiles it replaced or removed and the links
// it rebuilt, and takes its slots, places and links.
static void Apply(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    mnavMemory* memory = &navmesh->memory;
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        if (navmesh->slots[s].tile != plan->slots[s].tile)
        {
            FreeTile(memory, navmesh->slots[s].tile);
        }
    }
    for (int32_t r = 0; r < plan->relinkCount; ++r)
    {
        mnavRelink* relink = &plan->relinks[r];
        mnavTile* tile = plan->slots[relink->slot].tile;
        ReleaseLinks(memory, tile);
        tile->links = relink->links;
        tile->linkCount = relink->linkCount;
        tile->firstLink = relink->firstLink;
    }
    mnavRelease(memory, plan->relinks, (size_t)plan->relinkCapacity, sizeof(mnavRelink),
                alignof(mnavRelink));
    mnavRelease(memory, navmesh->slots, (size_t)navmesh->slotCapacity, sizeof(mnavSlot),
                alignof(mnavSlot));
    mnavRelease(memory, navmesh->places, (size_t)navmesh->placeCapacity, sizeof(mnavPlace),
                alignof(mnavPlace));
    navmesh->slots = plan->slots;
    navmesh->slotCount = plan->slotCount;
    navmesh->slotCapacity = plan->slotCapacity;
    navmesh->places = plan->places;
    navmesh->placeCount = plan->placeCount;
    navmesh->placeCapacity = plan->placeCapacity;
    navmesh->stagedCount = 0;
    *plan = (mnavTilePlan){0};
}

mnavResult mnavPlanTiles(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    *plan = (mnavTilePlan){0};
    if (navmesh->stagedCount == 0)
    {
        return mnav_success;
    }
    InsertionSortStaged(navmesh->staged, navmesh->stagedCount);
    mnavResult result = MakePlan(navmesh, plan);
    if (result != mnav_success)
    {
        ReleasePlan(&navmesh->memory, plan);
    }
    return result;
}

void mnavDropTilePlan(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    ReleasePlan(&navmesh->memory, plan);
}

void mnavApplyTiles(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    if (navmesh->stagedCount > 0)
    {
        Apply(navmesh, plan);
    }
}

mnavFrame mnavFrameOf(const mnavNavmesh* navmesh, int32_t x, int32_t z)
{
    const mnavBakeDef* def = &navmesh->def;
    double size = (double)def->tileCells * (double)def->cellSize;
    return (mnavFrame){def->origin.x + (double)x * size, def->origin.z + (double)z * size,
                       def->origin.y, (double)def->cellSize, (double)def->cellHeight};
}

// Whether a polygon id names a polygon of the navmesh now.
mnavResult mnavCheckPolygon(const mnavNavmesh* navmesh, mnavPolygonId id)
{
    if (id.slot == 0 || id.slot > (uint32_t)navmesh->slotCount || id.generation == 0)
    {
        return mnav_errorInvalid;
    }
    const mnavSlot* slot = &navmesh->slots[id.slot - 1];
    if (id.generation > slot->generation)
    {
        return mnav_errorInvalid;
    }
    if (id.generation != slot->generation || slot->tile == nullptr)
    {
        return mnav_errorStale;
    }
    return id.polygon < (uint32_t)slot->tile->mesh.polygonCount ? mnav_success : mnav_errorInvalid;
}

mnavPos3 mnavVertexWorld(const mnavFrame* f, const mnavMeshVertex* v)
{
    return (mnavPos3){f->x0 + v->x * f->cell, f->y0 + (v->y - MNAV_HEIGHT_OFFSET) * f->height,
                      f->z0 + v->z * f->cell};
}
