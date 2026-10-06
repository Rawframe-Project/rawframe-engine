// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007): items inserted, removed and moved by
// index, their extents and bound nodes following them, and the offset
// moved along so that what the viewport shows stays put.

#include "context.h"
#include "tree.h"
#include "virtual.h"
#include "virtual_store.h"

#include "maul-ui/virtual.h"

#include <math.h>
#include <string.h>

muiResult muiNode_GetItem(const muiContext* context, muiNodeId nodeId, uint32_t* indexOut)
{
    if (context == nullptr || indexOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    uint32_t item = context->lists.items[slot - 1];
    if (item == 0 || item == MUI_ITEM_REMOVED)
    {
        return mui_empty;
    }
    *indexOut = item - 1;
    return mui_success;
}

// The list an edit names and its slot; NULL with the status to return.
static muiVirtualEntry* Edited(muiContext* context, muiNodeId nodeId, uint32_t* slotOut,
                               muiResult* statusOut)
{
    uint32_t slot = muiResolveEdit(context, nodeId, statusOut);
    muiVirtualEntry* entry = slot != 0 ? muiVirtualEntryOf(context, slot) : nullptr;
    if (slot != 0 && entry == nullptr)
    {
        *statusOut = mui_empty;
    }
    *slotOut = slot;
    return entry;
}

// Gives an estimated list at slot room for count items, its extents moved
// if they must be; false when they do not fit.
static bool Resize(muiContext* context, uint32_t slot, muiVirtualEntry** entryInOut, uint32_t count)
{
    muiVirtualStore* store = &context->lists;
    muiVirtualEntry* entry = *entryInOut;
    uint32_t base = muiVirtualRoom(store, entry, count);
    if (base == store->itemCapacity)
    {
        // A purge reorders the entries.
        muiVirtualPurge(context);
        entry = muiVirtualEntryOf(context, slot);
        *entryInOut = entry;
        base = muiVirtualRoom(store, entry, count);
    }
    if (base == store->itemCapacity)
    {
        return false;
    }
    // It only grows: all its extents move.
    memmove(store->sizes + base, store->sizes + entry->base,
            (size_t)entry->list.count * sizeof(float));
    entry->base = base;
    return true;
}

// Moves the indices of the nodes bound to a list's items: those from
// first up to end by delta, and those of removed items to removed.
static void Rebind(muiContext* context, uint32_t slot, uint32_t first, uint32_t end, int64_t delta,
                   uint32_t removedFrom, uint32_t removedEnd)
{
    uint32_t* items = context->lists.items;
    const muiTree* tree = &context->tree;
    for (uint32_t c = muiTreeAt(tree, slot)->links.firstChild; c != 0;
         c = muiTreeAt(tree, c)->links.next)
    {
        // A removed item's index lies past every range.
        uint32_t item = items[c - 1];
        if (item == 0)
        {
            continue;
        }
        if (item - 1 >= removedFrom && item - 1 < removedEnd)
        {
            items[c - 1] = MUI_ITEM_REMOVED;
        }
        else if (item - 1 >= first && item - 1 < end)
        {
            items[c - 1] = (uint32_t)((int64_t)item + delta);
        }
    }
}

muiResult muiNode_InsertVirtualItems(muiContext* context, muiNodeId nodeId, uint32_t index,
                                     uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = 0;
    muiVirtualEntry* entry = Edited(context, nodeId, &slot, &status);
    if (entry == nullptr)
    {
        return status;
    }
    uint32_t total = entry->list.count;
    // Bound indices plus 1 stay below MUI_ITEM_REMOVED.
    if (index > total || count == 0 || count > MUI_ITEM_REMOVED - 1 - total)
    {
        return muiRefuse(context);
    }
    // Read before the storage moves.
    muiVirtualStore* store = &context->lists;
    double viewStart = muiVirtualViewStart(context, slot, entry);
    double before = muiVirtualOffset(store, entry, index);
    if (!entry->list.fixed)
    {
        if (!Resize(context, slot, &entry, total + count))
        {
            return mui_errorCapacity;
        }
        float* sizes = store->sizes + entry->base;
        memmove(sizes + index + count, sizes + index, (size_t)(total - index) * sizeof(float));
        for (uint32_t i = index; i < index + count; i++)
        {
            sizes[i] = entry->list.extent;
        }
    }
    entry->list.count = total + count;
    if (!entry->list.fixed)
    {
        muiVirtualBuild(store, entry);
    }
    Rebind(context, slot, index, total, count, 0, 0);
    if (before < viewStart)
    {
        muiVirtualShift(context, slot, entry,
                        muiVirtualOffset(store, entry, index + count) - before);
    }
    muiTreeMarkLayout(&context->tree, slot);
    return mui_success;
}

muiResult muiNode_RemoveVirtualItems(muiContext* context, muiNodeId nodeId, uint32_t index,
                                     uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = 0;
    muiVirtualEntry* entry = Edited(context, nodeId, &slot, &status);
    if (entry == nullptr)
    {
        return status;
    }
    uint32_t total = entry->list.count;
    if (index >= total || count == 0 || count > total - index)
    {
        return muiRefuse(context);
    }
    muiVirtualStore* store = &context->lists;
    double viewStart = muiVirtualViewStart(context, slot, entry);
    double before = muiVirtualOffset(store, entry, index);
    double after = muiVirtualOffset(store, entry, index + count);
    if (!entry->list.fixed)
    {
        float* sizes = store->sizes + entry->base;
        memmove(sizes + index, sizes + index + count,
                (size_t)(total - index - count) * sizeof(float));
    }
    entry->list.count = total - count;
    if (!entry->list.fixed)
    {
        muiVirtualBuild(store, entry);
    }
    Rebind(context, slot, index + count, total, -(int64_t)count, index, index + count);
    // The offset goes back by what was removed above the viewport's start.
    if (before < viewStart)
    {
        muiVirtualShift(context, slot, entry, before - fmin(after, viewStart));
    }
    muiTreeMarkLayout(&context->tree, slot);
    return mui_success;
}

muiResult muiNode_MoveVirtualItem(muiContext* context, muiNodeId nodeId, uint32_t from, uint32_t to)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = 0;
    muiVirtualEntry* entry = Edited(context, nodeId, &slot, &status);
    if (entry == nullptr)
    {
        return status;
    }
    uint32_t total = entry->list.count;
    if (from >= total || to >= total)
    {
        return muiRefuse(context);
    }
    if (from == to)
    {
        return mui_success;
    }
    muiVirtualStore* store = &context->lists;
    double viewStart = muiVirtualViewStart(context, slot, entry);
    bool wasAbove = muiVirtualOffset(store, entry, from) < viewStart;
    double place = muiVirtualOffset(store, entry, from + 1) - muiVirtualOffset(store, entry, from);
    if (!entry->list.fixed)
    {
        float* sizes = store->sizes + entry->base;
        float moved = sizes[from];
        if (from < to)
        {
            memmove(sizes + from, sizes + from + 1, (size_t)(to - from) * sizeof(float));
        }
        else
        {
            memmove(sizes + to + 1, sizes + to, (size_t)(from - to) * sizeof(float));
        }
        sizes[to] = moved;
        muiVirtualBuild(store, entry);
    }
    // The moved item's node first, out of the way of the shift.
    uint32_t* items = store->items;
    const muiTree* tree = &context->tree;
    uint32_t mover = 0;
    for (uint32_t c = muiTreeAt(tree, slot)->links.firstChild; c != 0 && mover == 0;
         c = muiTreeAt(tree, c)->links.next)
    {
        mover = items[c - 1] == from + 1 ? c : 0;
    }
    if (from < to)
    {
        Rebind(context, slot, from + 1, to + 1, -1, 0, 0);
    }
    else
    {
        Rebind(context, slot, to, from, 1, 0, 0);
    }
    if (mover != 0)
    {
        items[mover - 1] = to + 1;
    }
    // Leaving the space above the viewport pulls what is shown up; coming
    // into it pushes it down.
    bool isAbove = muiVirtualOffset(store, entry, to) < viewStart;
    if (wasAbove != isAbove)
    {
        muiVirtualShift(context, slot, entry, isAbove ? place : -place);
    }
    muiTreeMarkLayout(&context->tree, slot);
    return mui_success;
}
