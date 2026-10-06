// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007): the table of popups and their placement
// beside their anchors after layout, flipping then clamping per axis as
// Wayland's positioner does without resizing.

#include "maul-ui/popup.h"

#include "context.h"
#include "layout_node.h"
#include "notify.h"
#include "popup.h"
#include "popup_store.h"
#include "scroll.h"
#include "tree.h"

#include <math.h>

// The entry of the node at slot; NULL for a node that is no popup.
static muiPopupEntry* EntryOf(const muiContext* context, uint32_t slot)
{
    const muiPopupStore* store = &context->popups;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPopupEntry* entry = &store->entries[i];
        if (entry->node.index1 == slot && muiTreeResolve(&context->tree, entry->node) == slot)
        {
            return entry;
        }
    }
    return nullptr;
}

// Takes out the entries of nodes destroyed since.
static void Purge(muiContext* context)
{
    muiPopupStore* store = &context->popups;
    for (uint32_t i = store->count; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->entries[i - 1].node) == 0)
        {
            store->entries[i - 1] = store->entries[--store->count];
        }
    }
}

// The live entry of a node; NULL for a node that is no popup.
static muiPopupEntry* Find(const muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0)
    {
        *statusOut = mui_errorInvalid;
        return nullptr;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    muiPopupEntry* entry = slot != 0 ? EntryOf(context, slot) : nullptr;
    *statusOut = slot == 0 ? mui_errorStale : entry == nullptr ? mui_empty : mui_success;
    return entry;
}

static bool IsLength(float value)
{
    return isfinite(value) && value >= 0.0f;
}

muiPopup muiDefaultPopup(void)
{
    return (muiPopup){.side = mui_popupBelow, .align = mui_popupAlignStart, .lightDismiss = true};
}

muiResult muiNode_SetPopup(muiContext* context, muiNodeId nodeId, const muiPopup* popup)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (popup == nullptr || popup->anchor.index1 == 0 || popup->side > mui_popupCenter ||
        popup->align > mui_popupAlignEnd || !IsLength(popup->gap) || !IsLength(popup->margin))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    uint32_t anchor = muiTreeResolve(&context->tree, popup->anchor);
    if (anchor == 0)
    {
        return mui_errorStale;
    }
    if (anchor == slot)
    {
        return muiRefuse(context);
    }
    muiPopupStore* store = &context->popups;
    muiPopupEntry* entry = EntryOf(context, slot);
    if (entry == nullptr)
    {
        if (store->count == store->capacity)
        {
            Purge(context);
        }
        if (store->count == store->capacity)
        {
            return mui_errorCapacity;
        }
        entry = &store->entries[store->count++];
    }
    *entry = (muiPopupEntry){
        .node = muiTreeIdOf(&context->tree, slot), .popup = *popup, .serial = ++store->serial};
    return mui_success;
}

muiResult muiNode_GetPopup(const muiContext* context, muiNodeId nodeId, muiPopup* popupOut)
{
    if (context == nullptr || popupOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    const muiPopupEntry* entry = Find(context, nodeId, &status);
    if (entry != nullptr)
    {
        *popupOut = entry->popup;
    }
    return status;
}

muiResult muiNode_GetPopupSide(const muiContext* context, muiNodeId nodeId, muiPopupSide* sideOut)
{
    if (context == nullptr || sideOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    const muiPopupEntry* entry = Find(context, nodeId, &status);
    if (entry == nullptr || !entry->placed)
    {
        return entry == nullptr ? status : mui_empty;
    }
    *sideOut = entry->placedSide;
    return mui_success;
}

muiResult muiNode_ClearPopup(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    muiPopupEntry* entry = Find(context, nodeId, &status);
    if (status == mui_errorInvalid)
    {
        return muiRefuse(context);
    }
    if (entry != nullptr)
    {
        *entry = context->popups.entries[--context->popups.count];
    }
    return status == mui_empty ? mui_success : status;
}

// One axis of a box on the surface.
typedef struct Span
{
    double start;
    double end;
} Span;

// Where a popup of length size goes along the side's axis: after the
// anchor (right or down) or before it, the gap between; flipped to the
// other side when it overflows bounds there and the other has more room.
static double Beside(Span anchor, double size, double gap, Span bounds, bool* afterInOut)
{
    double before = anchor.start - gap - size;
    double after = anchor.end + gap;
    double roomBefore = anchor.start - gap - bounds.start;
    double roomAfter = bounds.end - anchor.end - gap;
    bool flip = *afterInOut ? after + size > bounds.end && roomBefore > roomAfter
                            : before < bounds.start && roomAfter > roomBefore;
    *afterInOut ^= flip;
    return *afterInOut ? after : before;
}

// Where a popup of length size goes across the side's axis: its start,
// center or end on the anchor's, from the axis's start or, reversed,
// from its end.
static double Along(Span anchor, double size, muiPopupAlign align, bool reversed)
{
    if (align == mui_popupAlignCenter)
    {
        return (anchor.start + anchor.end - size) / 2.0;
    }
    bool atEnd = (align == mui_popupAlignEnd) != reversed;
    return atEnd ? anchor.end - size : anchor.start;
}

// Clamps a start into bounds; one too long keeps its start edge at the
// bounds' start, or reversed, its end edge at their end.
static double Clamp(double start, double size, Span bounds, bool reversed)
{
    if (size > bounds.end - bounds.start)
    {
        return reversed ? bounds.end - size : bounds.start;
    }
    return fmin(fmax(start, bounds.start), bounds.end - size);
}

// The side a flip along an axis leaves.
static muiPopupSide Flipped(muiPopupSide side)
{
    static const muiPopupSide s_opposite[] = {mui_popupAbove, mui_popupBelow, mui_popupEnd,
                                              mui_popupStart, mui_popupCenter};
    return s_opposite[side];
}

// Moves the popup at slot popup so that its border box lands beside the
// anchor at slot anchor, within the root's box.
static void Place(muiContext* context, uint32_t root, muiPopupEntry* entry, uint32_t popup,
                  uint32_t anchor)
{
    const muiPopup* def = &entry->popup;
    const muiLayoutNode* anchorNode = &context->layout[anchor - 1];
    muiRect* rect = &context->layout[popup - 1].rect;
    const muiRect* rootRect = &context->layout[root - 1].rect;
    double ax = 0.0;
    double ay = 0.0;
    muiScrollOriginOf(context, root, anchor, &ax, &ay);
    const Span spanX = {ax, ax + (double)anchorNode->rect.width};
    const Span spanY = {ay, ay + (double)anchorNode->rect.height};
    double margin = (double)def->margin;
    const Span boundsX = {margin, (double)rootRect->width - margin};
    const Span boundsY = {margin, (double)rootRect->height - margin};
    double width = (double)rect->width;
    double height = (double)rect->height;
    double gap = (double)def->gap;
    bool rtl = anchorNode->rtl;
    double x = 0.0;
    double y = 0.0;
    bool after = false;
    muiPopupSide side = def->side;
    switch (def->side)
    {
    case mui_popupBelow:
    case mui_popupAbove:
        after = def->side == mui_popupBelow;
        y = Beside(spanY, height, gap, boundsY, &after);
        side = after ? mui_popupBelow : mui_popupAbove;
        x = Along(spanX, width, def->align, rtl);
        break;
    case mui_popupStart:
    case mui_popupEnd:
    {
        // Start is before, the left, unless right to left.
        bool asked = (def->side == mui_popupEnd) != rtl;
        after = asked;
        x = Beside(spanX, width, gap, boundsX, &after);
        side = after == asked ? def->side : Flipped(def->side);
        y = Along(spanY, height, def->align, false);
        break;
    }
    default:
        x = (spanX.start + spanX.end - width) / 2.0;
        y = (spanY.start + spanY.end - height) / 2.0;
        break;
    }
    x = Clamp(x, width, boundsX, rtl);
    y = Clamp(y, height, boundsY, false);
    double px = 0.0;
    double py = 0.0;
    muiScrollOriginOf(context, root, popup, &px, &py);
    rect->x = (float)((double)rect->x + x - px);
    rect->y = (float)((double)rect->y + y - py);
    entry->placedSide = side;
    entry->placed = true;
    if (!muiIsSameRect(*rect, context->draw.states[popup - 1].rect))
    {
        muiTreeMark(&context->tree, popup, mui_stagePaint);
    }
}

// The marks of entries while placing: not yet placed, or done (placed,
// or not placed this run).
enum
{
    MARK_WAITING = 0,
    MARK_DONE = 1,
};

// The index of the popup entry holding the node at slot, itself or its
// nearest ancestor that is a popup; the count for none.
static uint32_t HolderOf(const muiContext* context, uint32_t slot)
{
    const muiPopupStore* store = &context->popups;
    for (uint32_t at = slot; at != 0; at = muiTreeAt(&context->tree, at)->links.parent)
    {
        const muiPopupEntry* entry = EntryOf(context, at);
        if (entry != nullptr)
        {
            return (uint32_t)(entry - store->entries);
        }
    }
    return store->count;
}

// One pass over the entries waiting: places those whose holders are
// done, or with any, every one; whether it placed one.
static bool Pass(muiContext* context, uint32_t root, bool any)
{
    muiPopupStore* store = &context->popups;
    bool progress = false;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPopupEntry* entry = &store->entries[i];
        if (entry->mark == MARK_DONE || (!any && entry->holder != store->count &&
                                         store->entries[entry->holder].mark != MARK_DONE))
        {
            continue;
        }
        Place(context, root, entry, muiTreeResolve(&context->tree, entry->node),
              muiTreeResolve(&context->tree, entry->popup.anchor));
        entry->mark = MARK_DONE;
        progress = true;
    }
    return progress;
}

void muiPlacePopups(muiContext* context, uint32_t root)
{
    muiPopupStore* store = &context->popups;
    const muiTree* tree = &context->tree;
    // Each entry waits on the popup holding its anchor, placed first; ones
    // that cannot be placed under this root are done at once. The root has
    // no parent, so an anchor under it is under a popup that is the root.
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPopupEntry* entry = &store->entries[i];
        uint32_t popup = muiTreeResolve(tree, entry->node);
        uint32_t anchor = muiTreeResolve(tree, entry->popup.anchor);
        bool placeable = popup != 0 && anchor != 0 && muiTreeIsAncestor(tree, root, popup) &&
                         muiTreeIsAncestor(tree, root, anchor) &&
                         !muiTreeIsAncestor(tree, popup, anchor);
        entry->mark = placeable ? MARK_WAITING : MARK_DONE;
        entry->holder = placeable ? HolderOf(context, anchor) : store->count;
    }
    while (Pass(context, root, false))
    {
    }
    // What a cycle of anchors left, placed as it stands.
    (void)Pass(context, root, true);
}

// Whether an entry can be dismissed: live, light, not yet dismissed.
static bool IsOpen(const muiContext* context, const muiPopupEntry* entry)
{
    return entry->popup.lightDismiss && !entry->dismissed &&
           muiTreeResolve(&context->tree, entry->node) != 0;
}

static void Dismiss(muiContext* context, muiPopupEntry* entry, muiDismissReason reason)
{
    entry->dismissed = true;
    const muiNotification record = {mui_notificationPopupDismissed, entry->node, reason};
    muiNotifyPost(&context->notifications, &record);
}

// Dismisses, nested ones first, the open popups that neither hold the
// node at slot (0 for none) nor have their anchor hold it, keeping the
// popups those that do nest under.
static void DismissOutside(muiContext* context, uint32_t slot, muiDismissReason reason)
{
    muiPopupStore* store = &context->popups;
    const muiTree* tree = &context->tree;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPopupEntry* entry = &store->entries[i];
        uint32_t node = muiTreeResolve(tree, entry->node);
        uint32_t anchor = muiTreeResolve(tree, entry->popup.anchor);
        entry->holder = node != 0 && anchor != 0 ? HolderOf(context, anchor) : store->count;
        // No node holds a slot of 0.
        entry->mark = node != 0 && (muiTreeIsAncestor(tree, node, slot) ||
                                    (anchor != 0 && muiTreeIsAncestor(tree, anchor, slot)));
    }
    // The popups kept ones nest under are kept, and each is as deep as its
    // chain of holders, which a cycle of anchors cuts at the count.
    uint32_t deepest = 0;
    for (uint32_t i = 0; i < store->count; i++)
    {
        uint32_t depth = 0;
        for (uint32_t at = store->entries[i].holder; at != store->count && depth < store->count;
             at = store->entries[at].holder)
        {
            store->entries[at].mark |= store->entries[i].mark;
            depth++;
        }
        store->entries[i].depth = depth;
        deepest = depth > deepest ? depth : deepest;
    }
    for (uint32_t depth = deepest + 1; depth > 0; depth--)
    {
        for (uint32_t i = 0; i < store->count; i++)
        {
            muiPopupEntry* entry = &store->entries[i];
            if (entry->depth == depth - 1 && entry->mark == 0 && IsOpen(context, entry))
            {
                Dismiss(context, entry, reason);
            }
        }
    }
}

void muiPopupPress(muiContext* context, uint32_t slot)
{
    DismissOutside(context, slot, mui_dismissPress);
}

bool muiPopupEscape(muiContext* context)
{
    muiPopupStore* store = &context->popups;
    muiPopupEntry* last = nullptr;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPopupEntry* entry = &store->entries[i];
        if (IsOpen(context, entry) && (last == nullptr || entry->serial > last->serial))
        {
            last = entry;
        }
    }
    if (last != nullptr)
    {
        Dismiss(context, last, mui_dismissEscape);
    }
    return last != nullptr;
}

void muiPopupFocus(muiContext* context, uint32_t slot)
{
    if (slot != 0)
    {
        DismissOutside(context, slot, mui_dismissFocus);
    }
}
