// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runtime tiers (mnav-0004): reading the tier, and changing polygons'
// areas through the commit.

#include "modifiers.h"

#include "allocator.h"
#include "navmesh.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

#include <stdalign.h>
#include <stdint.h>

mnavTier mnavGetTier(const mnavNavmesh* navmesh)
{
    return navmesh != nullptr ? navmesh->def.tier : mnav_tierStatic;
}

mnavResult mnavStageArea(mnavNavmesh* navmesh, mnavPolygonId polygon, mnavAreaType area)
{
    if (navmesh == nullptr || area >= MNAV_AREA_TYPES)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavCheckPolygon(navmesh, polygon);
    if (result == mnav_success && navmesh->def.tier < mnav_tierModifiers)
    {
        result = mnav_errorTier;
    }
    if (result == mnav_success)
    {
        result = mnavReserve(&navmesh->memory, (void**)&navmesh->areaChanges,
                             &navmesh->areaChangeCapacity, navmesh->areaChangeCount,
                             navmesh->areaChangeCount + 1, sizeof(mnavAreaChange),
                             alignof(mnavAreaChange));
    }
    if (result == mnav_success)
    {
        navmesh->areaChanges[navmesh->areaChangeCount++] = (mnavAreaChange){polygon, area};
    }
    return result;
}

mnavResult mnavGetArea(const mnavNavmesh* navmesh, mnavPolygonId polygon, mnavAreaType* areaOut)
{
    if (navmesh == nullptr || areaOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavCheckPolygon(navmesh, polygon);
    if (result == mnav_success)
    {
        *areaOut = navmesh->slots[polygon.slot - 1].tile->mesh.polygons[polygon.polygon].area;
    }
    return result;
}

void mnavApplyAreas(mnavNavmesh* navmesh)
{
    for (int32_t i = 0; i < navmesh->areaChangeCount; ++i)
    {
        const mnavAreaChange* change = &navmesh->areaChanges[i];
        // A tile replaced or removed by this commit drops its changes.
        if (mnavCheckPolygon(navmesh, change->polygon) == mnav_success)
        {
            mnavPolygonId id = change->polygon;
            navmesh->slots[id.slot - 1].tile->mesh.polygons[id.polygon].area = change->area;
        }
    }
    navmesh->areaChangeCount = 0;
}
