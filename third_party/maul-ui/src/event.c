// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input (record mui-0007). The route is the target and its
// ancestors, written down before the first call; the host's function
// hears it from the top down, then from the target up, until it handles
// it. Unhandled keys and navigation then do what the library does by
// default: move the focus.

#include "maul-ui/event.h"

#include "context.h"
#include "event.h"
#include "focus.h"
#include "navigate.h"
#include "pointer.h"
#include "popup.h"
#include "range.h"
#include "scroll.h"
#include "tree.h"

#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"

#include <math.h>

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

bool muiMayFeed(const muiContext* context)
{
    return !muiIsInHostCall(context) && !context->events.dispatching;
}

bool muiRouteTo(muiContext* context, uint32_t target, const muiEvent* event)
{
    muiEventStore* store = &context->events;
    // A target of 0 has an empty route.
    if (store->function == nullptr)
    {
        return false;
    }
    const muiTree* tree = &context->tree;
    uint32_t count = 0;
    for (uint32_t at = target; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        store->route[count++] = muiTreeIdOf(tree, at);
    }
    store->dispatching = true;
    bool handled = false;
    for (uint32_t i = count; i > 0 && !handled; i--)
    {
        if (muiTreeResolve(tree, store->route[i - 1]) != 0)
        {
            handled = store->function(store->user, store->route[i - 1], mui_phaseTunnel, event);
        }
    }
    for (uint32_t i = 0; i < count && !handled; i++)
    {
        if (muiTreeResolve(tree, store->route[i]) != 0)
        {
            handled = store->function(store->user, store->route[i], mui_phaseBubble, event);
        }
    }
    store->dispatching = false;
    return handled;
}

// The node a player's input goes to under root: its focus, the top modal
// layer when that covers the focus, or root.
static uint32_t TargetOf(const muiContext* context, uint32_t root, uint8_t player)
{
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    return focus != 0 ? focus : scope;
}

// Checks what every input call checks; the root's slot, or 0 with the
// status to return.
static uint32_t Admit(muiContext* context, muiNodeId rootId, uint8_t player, bool valid,
                      muiResult* statusOut)
{
    if (!valid || IsNull(rootId) || player >= MUI_MAX_PLAYERS || !muiMayFeed(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    *statusOut = root != 0 ? mui_success : mui_errorStale;
    return root;
}

muiResult muiSetEventFunction(muiContext* context, muiEventFunction function, void* user)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiMayFeed(context))
    {
        return muiRefuse(context);
    }
    context->events.function = function;
    context->events.user = user;
    return mui_success;
}

// Moves a player's focus as an unhandled key or action asks: sequentially
// or toward a direction; whether it moved.
static bool MoveFocus(muiContext* context, muiNodeId rootId, uint8_t player, bool sequential,
                      bool backward, muiDirection direction)
{
    muiResult result = sequential ? muiFocus_Move(context, rootId, player, backward)
                                  : muiFocus_MoveToward(context, rootId, player, direction);
    return result == mui_success;
}

// The direction an arrow key names, or 4 for another key.
// A direction's default: inside a scroll container along its axis,
// Android's ScrollView rule, a focus move to a candidate within half a
// scrollport of the visible part (or one a link leads out to), else a
// line's step while the container can move; otherwise, or at its end, a
// focus move as muiFocus_MoveToward makes (record mui-0007).
static bool Toward(muiContext* context, muiNodeId rootId, uint32_t root, uint8_t player,
                   muiDirection direction, uint64_t timeNs)
{
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    bool horizontal = direction == mui_directionLeft || direction == mui_directionRight;
    uint32_t container = focus != 0 ? muiScrollerOf(context, scope, focus, horizontal) : 0;
    if (container != 0)
    {
        uint32_t next = muiNavigateFind(context, container, focus, direction);
        if (next != 0 && (!muiTreeIsAncestor(&context->tree, container, next) ||
                          muiScrollIsNear(context, container, next, horizontal)))
        {
            muiFocusNavigate(context, player, next);
            return true;
        }
        if (muiScrollLine(context, container, direction, timeNs))
        {
            return true;
        }
    }
    return MoveFocus(context, rootId, player, false, false, direction);
}

// A page key's default: a step of the scroll container holding the
// player's focus (or its scope's), vertically.
static bool Page(muiContext* context, uint32_t root, uint8_t player, muiKeyCode code, bool backward,
                 uint64_t timeNs)
{
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    uint32_t container = muiScrollerOf(context, scope, focus != 0 ? focus : scope, false);
    return container != 0 && muiScrollPage(context, container, code, backward, timeNs);
}

static uint32_t ArrowOf(muiKeyCode code)
{
    switch (code)
    {
    case mui_codeArrowUp:
        return mui_directionUp;
    case mui_codeArrowDown:
        return mui_directionDown;
    case mui_codeArrowLeft:
        return mui_directionLeft;
    case mui_codeArrowRight:
        return mui_directionRight;
    default:
        return 4;
    }
}

muiResult muiKeyInput(muiContext* context, muiNodeId rootId, const muiKeyEvent* event,
                      bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid = event != nullptr && handledOut != nullptr;
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    if (event->down)
    {
        context->focus.showsByCode[event->player] = true;
    }
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = event->down ? mui_eventKeyDown : mui_eventKeyUp,
        .player = event->player,
        .repeat = event->repeat,
        .modifiers = event->modifiers,
        .code = event->code,
        .key = event->key,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
    };
    bool handled = muiRouteTo(context, target, &routed);
    const muiModifiers held =
        event->modifiers & (mui_modShift | mui_modControl | mui_modAlt | mui_modMeta);
    if (!handled && event->down)
    {
        uint32_t arrow = ArrowOf(event->code);
        if (event->code == mui_codeEscape && (muiCancelDrags(context) || muiPopupEscape(context)))
        {
            handled = true;
        }
        else if (event->code == mui_codeTab && (held & ~mui_modShift) == 0)
        {
            handled = MoveFocus(context, rootId, event->player, true, held != 0, 0);
        }
        else if (held == 0 && muiRangeKey(context, target, event->code))
        {
            handled = true;
        }
        else if (arrow != 4 && held == 0)
        {
            handled =
                Toward(context, rootId, root, event->player, (muiDirection)arrow, event->timeNs);
        }
        else if ((event->code == mui_codePageUp || event->code == mui_codePageDown ||
                  event->code == mui_codeHome || event->code == mui_codeEnd) &&
                 held == 0)
        {
            handled = Page(context, root, event->player, event->code, event->code == mui_codePageUp,
                           event->timeNs);
        }
        else if (event->code == mui_codeSpace && (held & ~mui_modShift) == 0)
        {
            handled = Page(context, root, event->player, event->code, held != 0, event->timeNs);
        }
    }
    *handledOut = handled;
    return mui_success;
}

muiResult muiTextInput(muiContext* context, muiNodeId rootId, const muiTextEvent* event,
                       bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid =
        event != nullptr && handledOut != nullptr && (event->text != nullptr || event->length == 0);
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = mui_eventText,
        .player = event->player,
        .length = event->length,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
        .text = event->text,
    };
    *handledOut = muiRouteTo(context, target, &routed);
    return mui_success;
}

muiResult muiNavigationInput(muiContext* context, muiNodeId rootId, const muiNavigationEvent* event,
                             bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid = event != nullptr && handledOut != nullptr && event->action <= mui_navigateCancel;
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    context->focus.showsByCode[event->player] = true;
    uint32_t target = TargetOf(context, root, event->player);
    const muiEvent routed = {
        .kind = mui_eventNavigation,
        .player = event->player,
        .navigation = event->action,
        .timeNs = event->timeNs,
        .target = muiTreeIdOf(&context->tree, target),
    };
    bool handled = muiRouteTo(context, target, &routed);
    if (!handled && event->action <= mui_navigateRight)
    {
        // The directions are numbered as muiDirection's.
        handled = muiRangeDirection(context, target, (muiDirection)event->action) ||
                  Toward(context, rootId, root, event->player, (muiDirection)event->action,
                         event->timeNs);
    }
    else if (!handled && event->action <= mui_navigatePrevious)
    {
        handled = MoveFocus(context, rootId, event->player, true,
                            event->action == mui_navigatePrevious, 0);
    }
    *handledOut = handled;
    return mui_success;
}

muiResult muiWheelInput(muiContext* context, muiNodeId rootId, const muiWheelEvent* event,
                        bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    bool valid = event != nullptr && handledOut != nullptr && isfinite(event->x) &&
                 isfinite(event->y) && isfinite(event->deltaX) && isfinite(event->deltaY);
    uint32_t root = Admit(context, rootId, valid ? event->player : 0, valid, &status);
    if (root == 0)
    {
        return status;
    }
    muiHit hit = {0};
    (void)muiHitTest(context, rootId, event->x, event->y, &hit);
    uint32_t target = muiTreeResolve(&context->tree, hit.node);
    bool handled = false;
    if (target != 0)
    {
        const muiEvent routed = {
            .kind = mui_eventWheel,
            .player = event->player,
            .modifiers = event->modifiers,
            .timeNs = event->timeNs,
            .target = hit.node,
            .wheel = event,
        };
        handled =
            muiRouteTo(context, target, &routed) || muiScrollWheel(context, root, hit.node, event);
    }
    *handledOut = handled;
    return mui_success;
}

muiResult muiDispatchPointerRecord(muiContext* context, const muiPointerRecord* record,
                                   bool* handledOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (record == nullptr || handledOut == nullptr || !muiMayFeed(context))
    {
        return muiRefuse(context);
    }
    uint32_t target = IsNull(record->node) ? 0 : muiTreeResolve(&context->tree, record->node);
    const muiEvent routed = {
        .kind = mui_eventPointer,
        .timeNs = record->timeNs,
        .target = record->node,
        .pointer = record,
    };
    *handledOut = muiRouteTo(context, target, &routed) || muiRangePointer(context, record) ||
                  muiScrollPointer(context, record);
    return mui_success;
}
