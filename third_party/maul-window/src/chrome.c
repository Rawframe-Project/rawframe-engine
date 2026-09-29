// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Custom chrome.

#include "chrome.h"

#include <math.h>
#include <string.h>

static bool IsRegion(const mwinHitRegion* region)
{
    const mwinRect* rect = &region->rect;
    return isfinite(rect->x) && isfinite(rect->y) && isfinite(rect->width) &&
           isfinite(rect->height) && rect->width >= 0.0f && rect->height >= 0.0f &&
           region->kind <= mwin_hitClose;
}

mwinResult mwinRequestHitRegions(mwinContext* context, mwinWindowId window,
                                 const mwinHitRegion* regions, uint32_t count,
                                 mwinRequestId* requestOut)
{
    bool valid = count <= MWIN_HIT_REGIONS && (regions != nullptr || count == 0);
    for (uint32_t i = 0; valid && i < count; i++)
    {
        valid = IsRegion(&regions[i]);
    }
    if (!valid)
    {
        return mwin_errorInvalid;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestHitRegions, &slot, &request);
    if (status == mwin_success)
    {
        // Every backend takes them as they are: the hit tests read them.
        mwinWindow* found = &context->windows[slot];
        if (count > 0)
        {
            memcpy(found->regions, regions, count * sizeof(mwinHitRegion));
        }
        found->regionCount = (uint8_t)count;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinHitKind mwinHitAt(const mwinWindow* window, float x, float y)
{
    for (uint32_t i = window->regionCount; i > 0; i--)
    {
        const mwinRect* rect = &window->regions[i - 1].rect;
        if (x >= rect->x && y >= rect->y && x < rect->x + rect->width && y < rect->y + rect->height)
        {
            return window->regions[i - 1].kind;
        }
    }
    return mwin_hitClient;
}
