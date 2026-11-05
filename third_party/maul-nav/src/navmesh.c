// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh: staged tiles, atomic commits and links across tile sides.

#include "navmesh.h"

#include "allocator.h"
#include "bake_def.h"
#include "detail.h"
#include "fpenv.h"
#include "invariant.h"
#include "outputs.h"
#include "polymesh.h"
#include "tile.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static mnavNavmeshDefResult Refuse(mnavResult result, mnavNavmeshSetting setting)
{
    return (mnavNavmeshDefResult){result, setting, mnav_bakeSettingNone};
}

static mnavNavmeshDefResult CheckLimits(const mnavNavmeshLimits* limits)
{
    const struct
    {
        int32_t value;
        int32_t max;
        mnavNavmeshSetting setting;
    } counts[] = {
        {limits->tiles, MNAV_MAX_TILES, mnav_navmeshSettingTiles},
        {limits->tileLinks, MNAV_MAX_TILE_LINKS, mnav_navmeshSettingTileLinks},
        {limits->links, MNAV_MAX_LINKS, mnav_navmeshSettingLinks},
    };
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        if (counts[i].value < 1)
        {
            return Refuse(mnav_errorInvalid, counts[i].setting);
        }
        if (counts[i].value > counts[i].max)
        {
            return Refuse(mnav_errorLimit, counts[i].setting);
        }
    }
    if (limits->memoryBytes == 0)
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingMemoryBytes);
    }
    return Refuse(mnav_success, mnav_navmeshSettingNone);
}

// Checks a navmesh def and converts its bake def's agent to cells.
static mnavNavmeshDefResult CheckDef(const mnavNavmeshDef* def, mnavBakeCells* cells)
{
    if (def == nullptr)
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingNone);
    }
    if (def->cookie != MNAV_NAVMESH_DEF_COOKIE)
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingCookie);
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return Refuse(mnav_errorVersion, mnav_navmeshSettingNone);
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingAllocator);
    }
    mnavBakeDefResult bake = mnavCheckBakeDef(&def->bake, cells);
    if (bake.result != mnav_success)
    {
        return (mnavNavmeshDefResult){bake.result, mnav_navmeshSettingBake, bake.setting};
    }
    if ((uint32_t)def->tier > mnav_tierDynamic)
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingTier);
    }
    return CheckLimits(&def->limits);
}

static MNAV_NOINLINE mnavNavmeshDefResult CreateNavmeshWork(const mnavNavmeshDef* def,
                                                            mnavNavmesh** navmeshOut)
{
    if (navmeshOut == nullptr)
    {
        return Refuse(mnav_errorInvalid, mnav_navmeshSettingNone);
    }
    *navmeshOut = nullptr;
    mnavBakeCells cells = {0};
    mnavNavmeshDefResult checked = CheckDef(def, &cells);
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
        return Refuse(result, mnav_navmeshSettingNone);
    }
    *navmesh = (mnavNavmesh){0};
    for (int32_t k = 0; k < MNAV_LINK_KINDS; ++k)
    {
        navmesh->costPerMeter[k] = (double)INFINITY;
    }
    navmesh->def = *def;
    navmesh->cells = cells;
    navmesh->linkSlotLimit = MNAV_MAX_LINKS;
    navmesh->memory = memory;
    navmesh->memory.host = &navmesh->host;
    *navmeshOut = navmesh;
    return Refuse(mnav_success, mnav_navmeshSettingNone);
}

mnavNavmeshDefResult mnavCreateNavmesh(const mnavNavmeshDef* def, mnavNavmesh** navmeshOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavNavmeshDefResult result = CreateNavmeshWork(def, navmeshOut);
    mnavFloatEnvLeave(env);
    return result;
}

uint64_t mnavNavmesh_GetMisuse(const mnavNavmesh* navmesh)
{
    return navmesh != nullptr ? mnavHostMisuse(&navmesh->host) : 0;
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
    if (navmesh == nullptr || mnavHostCalledBack(&navmesh->host))
    {
        return;
    }
    // Each tile leaves its slot before it is freed, so a free hook given
    // one of its ids finds it stale; each array is emptied before it is
    // freed, so a later read finds nothing there rather than released
    // memory.
    mnavMemory* memory = &navmesh->memory;
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        mnavTile* tile = navmesh->staged[s].tile;
        navmesh->staged[s].tile = nullptr;
        FreeTile(memory, tile);
    }
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        mnavTile* tile = navmesh->slots[s].tile;
        navmesh->slots[s].tile = nullptr;
        FreeTile(memory, tile);
    }
    navmesh->stagedCount = 0;
    mnavRelease(memory, navmesh->staged, (size_t)navmesh->stagedCapacity, sizeof(mnavStaged),
                alignof(mnavStaged));
    navmesh->linkSlots = 0;
    mnavRelease(memory, navmesh->links, (size_t)navmesh->linkCapacity, sizeof(mnavOffLink),
                alignof(mnavOffLink));
    navmesh->areaChangeCount = 0;
    mnavRelease(memory, navmesh->areaChanges, (size_t)navmesh->areaChangeCapacity,
                sizeof(mnavAreaChange), alignof(mnavAreaChange));
    navmesh->arrivalCount = 0;
    mnavRelease(memory, navmesh->arrivals, (size_t)navmesh->arrivalCapacity, sizeof(uint64_t),
                alignof(uint64_t));
    navmesh->attachmentCount = 0;
    mnavRelease(memory, navmesh->attachments, (size_t)navmesh->attachmentCapacity, sizeof(uint64_t),
                alignof(uint64_t));
    navmesh->placeCount = 0;
    mnavRelease(memory, navmesh->places, (size_t)navmesh->placeCapacity, sizeof(mnavPlace),
                alignof(mnavPlace));
    navmesh->slotCount = 0;
    mnavRelease(memory, navmesh->slots, (size_t)navmesh->slotCapacity, sizeof(mnavSlot),
                alignof(mnavSlot));
    // The mark lives in the block being freed: it is set for good, so that
    // a call back in from this release is refused too, and the release
    // does not touch it.
    mnavHostEnter(&navmesh->host);
    mnavMemory last = *memory;
    last.host = nullptr;
    mnavRelease(&last, navmesh, 1, sizeof(mnavNavmesh), alignof(mnavNavmesh));
}

MNAV_NODISCARD static bool Before(int32_t ax, int32_t az, int32_t bx, int32_t bz)
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

static mnavResult GetTileWork(const mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ,
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

mnavResult mnavNavmesh_GetTile(const mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ,
                               mnavTileId* tileOut)
{
    mnavClearOutput(tileOut, sizeof(*tileOut));
    mnavResult result =
        mnavHostCountInvalid(MNAV_HOST(navmesh), GetTileWork(navmesh, tileX, tileZ, tileOut));
    return mnavClearOnFailure(result, tileOut, sizeof(*tileOut));
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
MNAV_NODISCARD static bool Matches(const mnavNavmesh* navmesh, const mnavTileInfo* info)
{
    const mnavBakeDef* def = &navmesh->def.bake;
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

static MNAV_NOINLINE mnavTileResult StageTileWork(mnavNavmesh* navmesh, const uint8_t* bytes,
                                                  size_t size)
{
    if (navmesh != nullptr && mnavHostCalledBackWriting(&navmesh->host))
    {
        return (mnavTileResult){mnav_errorBusy, mnav_tileHeader, -1};
    }
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
    // Settings of another def are a check on the bytes too.
    if (result.result == mnav_success && !Matches(navmesh, &tile->info))
    {
        result = (mnavTileResult){mnav_errorFormat, mnav_tileHeader, -1};
    }
    int32_t held = 0;
    if (result.result == mnav_success && navmesh->def.tier < mnav_tierDynamic &&
        mnavTileAt(navmesh, tile->info.x, tile->info.z, &held) != nullptr)
    {
        result = (mnavTileResult){mnav_errorUnsupported, mnav_tileHeader, -1};
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

static MNAV_NOINLINE mnavTileResult StageTileCounted(mnavNavmesh* navmesh, const uint8_t* bytes,
                                                     size_t size)
{
    mnavTileResult result = StageTileWork(navmesh, bytes, size);
    result.result = mnavHostCountInvalid(MNAV_HOST(navmesh), result.result);
    return result;
}

mnavTileResult mnavNavmesh_StageTile(mnavNavmesh* navmesh, const uint8_t* bytes, size_t size)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavTileResult result = StageTileCounted(navmesh, bytes, size);
    mnavFloatEnvLeave(env);
    return result;
}

static mnavResult StageTileRemovalWork(mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ)
{
    if (navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (mnavHostCalledBackWriting(&navmesh->host))
    {
        return mnav_errorBusy;
    }
    return Stage(navmesh, tileX, tileZ, nullptr);
}

mnavResult mnavNavmesh_StageTileRemoval(mnavNavmesh* navmesh, int32_t tileX, int32_t tileZ)
{
    return mnavHostCountInvalid(MNAV_HOST(navmesh), StageTileRemovalWork(navmesh, tileX, tileZ));
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
MNAV_NODISCARD static bool CloseAt(const Span* a, const Span* b, int64_t u, int64_t step)
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
MNAV_NODISCARD static bool Overlap(const Span* a, const Span* b, int64_t step, int32_t* low,
                                   int32_t* high)
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
    result = relink.linkCount > navmesh->def.limits.tileLinks ? mnav_errorCapacity : mnav_success;
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
    // MakePlan took a slot for every staged change.
    MNAV_ASSERT(plan->slotCount < plan->slotCapacity);
    plan->slots[plan->slotCount] = (mnavSlot){0, false, 0, 0, nullptr};
    return plan->slotCount++;
}

// Puts a staged change into the planned slots: a replacement keeps its
// slot with the next generation, unless that would wrap, which retires
// the slot; a removal empties its slot with the next generation, and
// retires it when that is the last, since a free slot is taken at the
// generation after.
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
        slot->retired = slot->generation == UINT32_MAX;
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

// Appends a place to the planned places, which stay sorted with each
// place once, within the room MakePlan took for the committed places and
// the staged changes, and each naming a slot that holds a tile.
static void AddPlace(mnavTilePlan* plan, mnavPlace place)
{
    MNAV_ASSERT(plan->placeCount < plan->placeCapacity);
    MNAV_ASSERT(plan->slots[place.slot].tile != nullptr);
    MNAV_ASSERT(plan->placeCount == 0 ||
                Before(plan->places[plan->placeCount - 1].x, plan->places[plan->placeCount - 1].z,
                       place.x, place.z));
    plan->places[plan->placeCount++] = place;
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
            AddPlace(plan, *old);
            a += 1;
            continue;
        }
        // Stage keeps one change a place.
        MNAV_ASSERT(b == 0 || Before(navmesh->staged[b - 1].x, navmesh->staged[b - 1].z, change->x,
                                     change->z));
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
                AddPlace(plan, (mnavPlace){change->x, change->z, s});
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
MNAV_NODISCARD static bool NearChange(const mnavNavmesh* navmesh, const mnavSlot* slot)
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
        return mnav_errorCapacity;
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

// Swaps the plan in, then frees what it replaced: the old tiles, the
// links of each rebuilt tile, and the old slots and places. Every free runs
// with the plan in place, so a free hook that reads the navmesh back finds
// a removed or replaced tile's ids stale and each rebuilt tile with its new
// links.
static void Apply(mnavNavmesh* navmesh, mnavTilePlan* plan)
{
    mnavMemory* memory = &navmesh->memory;
    // Each relink takes the links its tile gives up.
    for (int32_t r = 0; r < plan->relinkCount; ++r)
    {
        mnavRelink* relink = &plan->relinks[r];
        mnavTile* tile = plan->slots[relink->slot].tile;
        mnavRelink old = {relink->slot, tile->links, tile->linkCount, tile->firstLink};
        tile->links = relink->links;
        tile->linkCount = relink->linkCount;
        tile->firstLink = relink->firstLink;
        *relink = old;
    }
    const mnavTilePlan old = {
        .slots = navmesh->slots,
        .slotCount = navmesh->slotCount,
        .slotCapacity = navmesh->slotCapacity,
        .places = navmesh->places,
        .placeCount = navmesh->placeCount,
        .placeCapacity = navmesh->placeCapacity,
        .relinks = plan->relinks,
        .relinkCount = plan->relinkCount,
        .relinkCapacity = plan->relinkCapacity,
    };
    navmesh->slots = plan->slots;
    navmesh->slotCount = plan->slotCount;
    navmesh->slotCapacity = plan->slotCapacity;
    navmesh->places = plan->places;
    navmesh->placeCount = plan->placeCount;
    navmesh->placeCapacity = plan->placeCapacity;
    navmesh->stagedCount = 0;
    *plan = (mnavTilePlan){0};
    for (int32_t s = 0; s < old.slotCount; ++s)
    {
        if (old.slots[s].tile != navmesh->slots[s].tile)
        {
            FreeTile(memory, old.slots[s].tile);
        }
    }
    for (int32_t r = 0; r < old.relinkCount; ++r)
    {
        const mnavRelink* relink = &old.relinks[r];
        int32_t polygons = navmesh->slots[relink->slot].tile->mesh.polygonCount;
        mnavRelease(memory, relink->links, (size_t)relink->linkCount, sizeof(mnavLink),
                    alignof(mnavLink));
        mnavRelease(memory, relink->firstLink, (size_t)polygons + 1, sizeof(int32_t),
                    alignof(int32_t));
    }
    mnavRelease(memory, old.relinks, (size_t)old.relinkCapacity, sizeof(mnavRelink),
                alignof(mnavRelink));
    mnavRelease(memory, old.slots, (size_t)old.slotCapacity, sizeof(mnavSlot), alignof(mnavSlot));
    mnavRelease(memory, old.places, (size_t)old.placeCapacity, sizeof(mnavPlace),
                alignof(mnavPlace));
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
    const mnavBakeDef* def = &navmesh->def.bake;
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

bool mnavInQueryReachGround(const mnavNavmesh* navmesh, double x, double z)
{
    const mnavPos3* o = &navmesh->def.bake.origin;
    return fabs(x - o->x) <= MNAV_MAX_QUERY_DISTANCE && fabs(z - o->z) <= MNAV_MAX_QUERY_DISTANCE;
}

bool mnavInQueryReach(const mnavNavmesh* navmesh, mnavPos3 p)
{
    return mnavInQueryReachGround(navmesh, p.x, p.z) &&
           fabs(p.y - navmesh->def.bake.origin.y) <= MNAV_MAX_QUERY_DISTANCE;
}
