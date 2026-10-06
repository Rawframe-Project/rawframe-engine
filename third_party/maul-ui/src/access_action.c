// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): requests from assistive technology,
// performed by the library where it owns what they change, routed to
// the node as activation for a click, posted to the host otherwise.

#include "access.h"
#include "context.h"
#include "event.h"
#include "layout_node.h"
#include "notify.h"
#include "range.h"
#include "scroll_store.h"
#include "tree.h"

#include "maul-ui/access.h"
#include "maul-ui/focus.h"
#include "maul-ui/scroll.h"

#include <math.h>

// Scrolls a container a page toward a direction, at once: a page as its
// page keys step one; whether it moved.
static bool Page(muiContext* context, uint32_t slot, muiAccessAction action)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    bool horizontal = action == mui_actionScrollLeft || action == mui_actionScrollRight;
    float limit = muiScrollLimit(&layout->style, size, scroll, horizontal);
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    float page = (extent - limit) * context->scrolling.rule.pageFraction;
    page = action == mui_actionScrollUp || action == mui_actionScrollLeft ? -page : page;
    // Right to left, the offset grows toward the left.
    float x = scroll->x + (horizontal ? (layout->rtl ? -page : page) : 0.0f);
    float y = scroll->y + (horizontal ? 0.0f : page);
    float oldX = scroll->x;
    float oldY = scroll->y;
    muiNodeId nodeId = muiTreeIdOf(&context->tree, slot);
    (void)muiNode_SetScroll(context, nodeId, x, y);
    return scroll->x != oldX || scroll->y != oldY;
}

// Does what a request asks of the node at slot, which takes its action;
// whether anything happened.
static bool Perform(muiContext* context, uint32_t slot, const muiAccessRequest* request)
{
    muiNodeId nodeId = muiTreeIdOf(&context->tree, slot);
    switch (request->action)
    {
    case mui_actionClick:
    {
        const muiEvent event = {
            .kind = mui_eventNavigation,
            .navigation = mui_navigateActivate,
            .target = nodeId,
        };
        return muiRouteTo(context, slot, &event);
    }
    case mui_actionFocus:
        return muiFocus_Set(context, 0, nodeId, mui_focusByNavigation) == mui_success;
    case mui_actionBlur:
        return muiFocus_Set(context, 0, (muiNodeId){0, 0}, mui_focusByCode) == mui_success;
    case mui_actionIncrement:
    case mui_actionDecrement:
        return muiRangeStep(context, slot, request->action == mui_actionIncrement);
    case mui_actionSetValue:
        return muiRangeSet(context, slot, request->value);
    case mui_actionScrollIntoView:
        return muiNode_ScrollIntoView(context, nodeId) == mui_success;
    case mui_actionScrollUp:
    case mui_actionScrollDown:
    case mui_actionScrollLeft:
    case mui_actionScrollRight:
        return Page(context, slot, request->action);
    case mui_actionSetScrollOffset:
    {
        float x = 0.0f;
        float y = 0.0f;
        (void)muiNode_GetScroll(context, nodeId, &x, &y);
        (void)muiNode_SetScroll(context, nodeId, request->x, request->y);
        float movedX = 0.0f;
        float movedY = 0.0f;
        (void)muiNode_GetScroll(context, nodeId, &movedX, &movedY);
        return movedX != x || movedY != y;
    }
    default:
    {
        // Expand and collapse are the host's.
        const muiNotification record = {mui_notificationAccessAction, nodeId, request->action};
        muiNotifyPost(&context->notifications, &record);
        return true;
    }
    }
}

muiResult muiPerformAccessAction(muiContext* context, const muiAccessRequest* request,
                                 bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (handledOut != nullptr)
    {
        *handledOut = false;
    }
    if (request == nullptr || request->action > mui_actionSetScrollOffset ||
        !isfinite(request->value) || !isfinite(request->x) || !isfinite(request->y) ||
        !muiMayFeed(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, muiNodeIdOfAccess(request->target), &status);
    if (slot == 0)
    {
        return status;
    }
    // The node takes the action now, as an update would send it.
    muiAccessNode node;
    (void)muiAccessDerive(context, slot, &node, nullptr);
    if ((node.actions & (1u << request->action)) == 0)
    {
        return mui_empty;
    }
    bool handled = Perform(context, slot, request);
    if (handledOut != nullptr)
    {
        *handledOut = handled;
    }
    return mui_success;
}
