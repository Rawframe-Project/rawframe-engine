// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling's queries: a node's offset, its extent and its thumb, read
// from what layout measured.

#include "context.h"
#include "fpenv.h"
#include "layout_node.h"
#include "scroll_store.h"
#include "tree.h"

#include "maul-ui/scroll.h"

#include <math.h>
#include <string.h>

static uint32_t ResolveRead(const muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0)
    {
        *statusOut = mui_errorInvalid;
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

static MUI_NOINLINE muiResult NodeGetScrollThumbWorker(const muiContext* context, muiNodeId nodeId,
                                                       muiAxis axis, float track, float minimum,
                                                       muiScrollThumb* thumbOut)
{
    if (thumbOut != nullptr)
    {
        memset(thumbOut, 0, sizeof *thumbOut);
    }
    if (context == nullptr || thumbOut == nullptr || axis > mui_axisVertical || !isfinite(track) ||
        track < 0.0f || !isfinite(minimum) || minimum < 0.0f)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    bool horizontal = axis == mui_axisHorizontal;
    float limit = muiScrollLimit(&layout->style, size, scroll, horizontal);
    if (limit <= 0.0f)
    {
        *thumbOut = (muiScrollThumb){0.0f, track};
        return mui_success;
    }
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    float length = fminf(fmaxf(track * (extent - limit) / extent, minimum), track);
    float offset = horizontal ? scroll->x : scroll->y;
    // From the track's start, which is the scroll origin's side unless the
    // origin is the far edge of its top or inline start.
    bool far = horizontal ? scroll->right != layout->rtl : scroll->bottom;
    float placed = (track - length) * offset / limit;
    *thumbOut = (muiScrollThumb){far ? track - length - placed : placed, length};
    return mui_success;
}

muiResult muiNode_GetScrollThumb(const muiContext* context, muiNodeId nodeId, muiAxis axis,
                                 float track, float minimum, muiScrollThumb* thumbOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NodeGetScrollThumbWorker(context, nodeId, axis, track, minimum, thumbOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiNode_GetScroll(const muiContext* context, muiNodeId nodeId, float* xOut, float* yOut)
{
    if (xOut != nullptr)
    {
        *xOut = 0.0f;
    }
    if (yOut != nullptr)
    {
        *yOut = 0.0f;
    }
    if (context == nullptr || xOut == nullptr || yOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot != 0)
    {
        *xOut = context->scrolls[slot - 1].x;
        *yOut = context->scrolls[slot - 1].y;
    }
    return status;
}

muiResult muiNode_GetScrollExtent(const muiContext* context, muiNodeId nodeId, muiSize* extentOut)
{
    if (extentOut != nullptr)
    {
        memset(extentOut, 0, sizeof *extentOut);
    }
    if (context == nullptr || extentOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveRead(context, nodeId, &status);
    if (slot != 0)
    {
        const muiScrollState* scroll = &context->scrolls[slot - 1];
        *extentOut = (muiSize){scroll->extentWidth, scroll->extentHeight};
    }
    return status;
}
