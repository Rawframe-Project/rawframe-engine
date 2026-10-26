// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh's commit: tiles and off-mesh links together, all
// or nothing.

#include "fpenv.h"
#include "invariant.h"
#include "modifiers.h"
#include "navmesh.h"
#include "offmesh.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

// A link from polygon p of the tile in slot leaves an edge on the side it
// names, over part of it, to a committed polygon that has the same link
// back.
static void CheckLink(const mnavNavmesh* navmesh, int32_t slot, int32_t p, const mnavLink* link)
{
    const mnavSlot* own = &navmesh->slots[slot];
    const mnavPolygon* polygon = &own->tile->mesh.polygons[p];
    MNAV_ASSERT(link->polygon == p && link->edge < polygon->count);
    MNAV_ASSERT(link->side >= 1 && link->side <= 4 && polygon->sides[link->edge] == link->side);
    MNAV_ASSERT(link->low < link->high);
    const mnavTile* across = nullptr;
    MNAV_ASSERT(mnavPolygonOf(navmesh, link->target, &across) != nullptr);
    uint32_t q = link->target.polygon;
    bool back = false;
    for (int32_t k = across->firstLink[q]; k < across->firstLink[q + 1]; ++k)
    {
        const mnavLink* other = &across->links[k];
        back = back || (other->target.slot == (uint32_t)slot + 1 &&
                        other->target.generation == own->generation &&
                        other->target.polygon == (uint32_t)p && other->low == link->low &&
                        other->high == link->high);
    }
    MNAV_ASSERT(back);
}

// The places ascend by x, then z, each naming the occupied slot at it.
static void CheckPlaces(const mnavNavmesh* navmesh)
{
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        const mnavPlace* place = &navmesh->places[i];
        const mnavPlace* last = &navmesh->places[i > 0 ? i - 1 : 0];
        MNAV_ASSERT(i == 0 || last->x < place->x || (last->x == place->x && last->z < place->z));
        MNAV_ASSERT(place->slot >= 0 && place->slot < navmesh->slotCount);
        const mnavSlot* slot = &navmesh->slots[place->slot];
        MNAV_ASSERT(slot->tile != nullptr && slot->x == place->x && slot->z == place->z);
    }
}

// The tile in slot s is current, and its links lie polygon by polygon and
// hold (CheckLink).
static void CheckTile(const mnavNavmesh* navmesh, int32_t s)
{
    const mnavTile* tile = navmesh->slots[s].tile;
    MNAV_ASSERT(navmesh->slots[s].generation != 0 && !navmesh->slots[s].retired);
    MNAV_ASSERT(tile->firstLink[0] == 0 &&
                tile->firstLink[tile->mesh.polygonCount] == tile->linkCount);
    for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
    {
        MNAV_ASSERT(tile->firstLink[p] <= tile->firstLink[p + 1]);
        for (int32_t k = tile->firstLink[p]; k < tile->firstLink[p + 1]; ++k)
        {
            CheckLink(navmesh, s, p, &tile->links[k]);
        }
    }
}

// After a commit: the places hold (CheckPlaces) and every occupied slot
// has one, every tile holds (CheckTile), the attachment keys ascend, and
// nothing is left staged.
static void CheckNavmesh(const mnavNavmesh* navmesh)
{
    CheckPlaces(navmesh);
    int32_t occupied = 0;
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        if (navmesh->slots[s].tile != nullptr)
        {
            occupied += 1;
            CheckTile(navmesh, s);
        }
    }
    MNAV_ASSERT(occupied == navmesh->placeCount);
    for (int32_t i = 1; i < navmesh->attachmentCount; ++i)
    {
        MNAV_ASSERT(navmesh->attachments[i - 1] < navmesh->attachments[i]);
    }
    for (int32_t i = 1; i < navmesh->arrivalCount; ++i)
    {
        MNAV_ASSERT(navmesh->arrivals[i - 1] < navmesh->arrivals[i]);
    }
    MNAV_ASSERT(navmesh->stagedCount == 0 && navmesh->areaChangeCount == 0 &&
                navmesh->linksPending == 0);
}

static MNAV_NOINLINE mnavResult CommitWork(mnavNavmesh* navmesh)
{
    if (navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (navmesh->stagedCount == 0 && navmesh->linksPending == 0 && navmesh->areaChangeCount == 0)
    {
        return mnav_success;
    }
    // Every byte the commit needs is allocated before anything changes.
    mnavTilePlan tiles;
    mnavAttachmentPlan links = {0};
    mnavResult result = mnavPlanTiles(navmesh, &tiles);
    if (result == mnav_success)
    {
        result = mnavPlanAttachments(navmesh, &links);
        if (result != mnav_success)
        {
            mnavDropTilePlan(navmesh, &tiles);
        }
    }
    if (result == mnav_success)
    {
        mnavApplyTiles(navmesh, &tiles);
        mnavApplyAreas(navmesh);
        mnavApplyAttachments(navmesh, &links);
        navmesh->commits += 1;
        if (MNAV_INVARIANTS)
        {
            CheckNavmesh(navmesh);
        }
    }
    return result;
}

mnavResult mnavCommit(mnavNavmesh* navmesh)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = CommitWork(navmesh);
    mnavFloatEnvLeave(env);
    return result;
}
