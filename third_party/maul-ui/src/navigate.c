// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Directional navigation (record mui-0007). A link the focused node has
// for the direction decides first; otherwise every node sequential
// navigation reaches in the focus's layer is compared with the best so
// far as Android's focus search compares them: in the direction, in the
// focus's beam, then by a distance weighted toward the direction's axis.

#include "navigate.h"

#include "context.h"
#include "focus.h"
#include "scroll.h"
#include "tree.h"

#include "maul-ui/focus.h"

#include <math.h>

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

// A border box in its tree's space: every box moves by the same amount
// from the root's, which changes no comparison.
typedef struct Box
{
    double left;
    double top;
    double right;
    double bottom;
} Box;

static Box BoxOf(const muiContext* context, uint32_t slot)
{
    const muiRect* rect = &context->layout[slot - 1].rect;
    double x = 0.0;
    double y = 0.0;
    muiScrollOriginOf(context, 0, slot, &x, &y);
    return (Box){x, y, x + (double)rect->width, y + (double)rect->height};
}

// Whether dest lies in the direction from source.
static bool IsCandidate(const Box* source, const Box* dest, muiDirection direction)
{
    switch (direction)
    {
    case mui_directionLeft:
        return (source->right > dest->right || source->left >= dest->right) &&
               source->left > dest->left;
    case mui_directionRight:
        return (source->left < dest->left || source->right <= dest->left) &&
               source->right < dest->right;
    case mui_directionUp:
        return (source->bottom > dest->bottom || source->top >= dest->bottom) &&
               source->top > dest->top;
    default:
        return (source->top < dest->top || source->bottom <= dest->top) &&
               source->bottom < dest->bottom;
    }
}

static bool IsHorizontal(muiDirection direction)
{
    return direction == mui_directionLeft || direction == mui_directionRight;
}

// Whether two boxes overlap across the direction.
static bool BeamsOverlap(muiDirection direction, const Box* a, const Box* b)
{
    return IsHorizontal(direction) ? b->bottom > a->top && b->top < a->bottom
                                   : b->right > a->left && b->left < a->right;
}

// Whether dest lies wholly past source going up or down: only those
// directions ask.
static bool IsToDirectionOf(muiDirection direction, const Box* source, const Box* dest)
{
    return direction == mui_directionUp ? source->top >= dest->bottom : source->bottom <= dest->top;
}

// The gap from source to dest's near edge along the direction, at least 0.
static double MajorDistance(muiDirection direction, const Box* source, const Box* dest)
{
    double gap = 0.0;
    switch (direction)
    {
    case mui_directionLeft:
        gap = source->left - dest->right;
        break;
    case mui_directionRight:
        gap = dest->left - source->right;
        break;
    case mui_directionUp:
        gap = source->top - dest->bottom;
        break;
    default:
        gap = dest->top - source->bottom;
        break;
    }
    return fmax(0.0, gap);
}

// The distance from source to dest's far edge going up or down, at least
// 1: only those directions ask.
static double MajorDistanceToFarEdge(muiDirection direction, const Box* source, const Box* dest)
{
    double gap =
        direction == mui_directionUp ? source->top - dest->top : dest->bottom - source->bottom;
    return fmax(1.0, gap);
}

// The distance between the centers across the direction.
static double MinorDistance(muiDirection direction, const Box* source, const Box* dest)
{
    return IsHorizontal(direction)
               ? fabs((source->top + source->bottom) * 0.5 - (dest->top + dest->bottom) * 0.5)
               : fabs((source->left + source->right) * 0.5 - (dest->left + dest->right) * 0.5);
}

static double WeightedDistance(muiDirection direction, const Box* source, const Box* dest)
{
    double major = MajorDistance(direction, source, dest);
    double minor = MinorDistance(direction, source, dest);
    return 13.0 * major * major + minor * minor;
}

// Whether a, in source's beam, beats b, outside it.
static bool BeamBeats(muiDirection direction, const Box* source, const Box* a, const Box* b)
{
    if (BeamsOverlap(direction, source, b) || !BeamsOverlap(direction, source, a))
    {
        return false;
    }
    if (IsHorizontal(direction) || !IsToDirectionOf(direction, source, b))
    {
        return true;
    }
    return MajorDistance(direction, source, a) < MajorDistanceToFarEdge(direction, source, b);
}

// Whether a beats the best so far, b (a candidate, or none).
static bool IsBetter(muiDirection direction, const Box* source, const Box* a, const Box* b)
{
    if (!IsCandidate(source, a, direction))
    {
        return false;
    }
    if (b == nullptr)
    {
        return true;
    }
    if (BeamBeats(direction, source, a, b))
    {
        return true;
    }
    if (BeamBeats(direction, source, b, a))
    {
        return false;
    }
    return WeightedDistance(direction, source, a) < WeightedDistance(direction, source, b);
}

static const muiNeighbor* LinkOf(const muiContext* context, uint32_t slot, muiDirection direction)
{
    const muiFocusStore* store = &context->focus;
    uint32_t generation = muiTreeIdOf(&context->tree, slot).generation;
    for (uint32_t i = 0; i < store->neighborCount; i++)
    {
        const muiNeighbor* link = &store->neighbors[i];
        if (link->slot == slot && link->generation == generation && link->direction == direction)
        {
            return link;
        }
    }
    return nullptr;
}

// What a link sends focus to: the target (the focus itself stops the
// move), or 0 to leave it to geometry.
static uint32_t Linked(const muiContext* context, uint32_t focus, muiDirection direction)
{
    const muiNeighbor* link = LinkOf(context, focus, direction);
    uint32_t target = link != nullptr ? muiTreeResolve(&context->tree, link->target) : 0;
    return target != 0 && muiFocusTakes(context, target, mui_focusPointer) &&
                   !muiFocusIsCovered(context, target)
               ? target
               : 0;
}

uint32_t muiNavigateFind(const muiContext* context, uint32_t scope, uint32_t focus,
                         muiDirection direction)
{
    const muiTree* tree = &context->tree;
    uint32_t next = Linked(context, focus, direction);
    if (next == 0)
    {
        const Box source = BoxOf(context, focus);
        Box best = {0};
        for (uint32_t at = scope; at != 0; at = muiFocusFollowing(tree, scope, at))
        {
            // The focus's own box is no candidate.
            if (!muiFocusTakes(context, at, mui_focusAll))
            {
                continue;
            }
            const Box box = BoxOf(context, at);
            if (IsBetter(direction, &source, &box, next != 0 ? &best : nullptr))
            {
                next = at;
                best = box;
            }
        }
    }
    return next != focus ? next : 0;
}

muiResult muiFocus_MoveToward(muiContext* context, muiNodeId rootId, uint8_t player,
                              muiDirection direction)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (IsNull(rootId) || player >= MUI_MAX_PLAYERS || direction > mui_directionRight ||
        muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    const muiTree* tree = &context->tree;
    uint32_t root = muiTreeResolve(tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    if (focus == 0)
    {
        return muiFocus_Move(context, rootId, player, false);
    }
    uint32_t next = muiNavigateFind(context, scope, focus, direction);
    if (next == 0)
    {
        return mui_empty;
    }
    muiFocusNavigate(context, player, next);
    return mui_success;
}

// Takes out the links of nodes destroyed since.
static void Purge(muiContext* context)
{
    muiFocusStore* store = &context->focus;
    for (uint32_t i = store->neighborCount; i > 0; i--)
    {
        const muiNeighbor* link = &store->neighbors[i - 1];
        if (muiTreeResolve(&context->tree, (muiNodeId){link->slot, link->generation}) == 0)
        {
            store->neighbors[i - 1] = store->neighbors[--store->neighborCount];
        }
    }
}

muiResult muiNode_SetNeighbor(muiContext* context, muiNodeId nodeId, muiDirection direction,
                              muiNodeId targetId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (direction > mui_directionRight || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (!IsNull(targetId) && muiTreeResolve(&context->tree, targetId) == 0)
    {
        return mui_errorStale;
    }
    muiFocusStore* store = &context->focus;
    muiNeighbor* link = (muiNeighbor*)LinkOf(context, slot, direction);
    if (IsNull(targetId))
    {
        if (link != nullptr)
        {
            *link = store->neighbors[--store->neighborCount];
        }
        return mui_success;
    }
    if (link == nullptr)
    {
        if (store->neighborCount == store->neighborCapacity)
        {
            Purge(context);
        }
        if (store->neighborCount == store->neighborCapacity)
        {
            return mui_errorCapacity;
        }
        link = &store->neighbors[store->neighborCount++];
    }
    *link = (muiNeighbor){slot, muiTreeIdOf(&context->tree, slot).generation, direction, targetId};
    return mui_success;
}

muiNodeId muiNode_GetNeighbor(const muiContext* context, muiNodeId nodeId, muiDirection direction)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    const muiNeighbor* link = slot != 0 ? LinkOf(context, slot, direction) : nullptr;
    return link != nullptr ? link->target : (muiNodeId){0, 0};
}
