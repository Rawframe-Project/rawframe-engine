// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh's commit (mnav-0004): tiles and off-mesh links together, all
// or nothing.

#include "modifiers.h"
#include "navmesh.h"
#include "offmesh.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

mnavResult mnavCommit(mnavNavmesh* navmesh)
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
    }
    return result;
}
