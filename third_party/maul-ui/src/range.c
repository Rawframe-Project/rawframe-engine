// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Range values (record mui-0007): the table of ranges, values kept on
// their steps, and the defaults keys and pointer records reach them by.

#include "maul-ui/range.h"

#include "context.h"
#include "notify.h"
#include "range.h"
#include "range_store.h"
#include "tree.h"

#include <math.h>

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

// The live entry of the node at slot; NULL for a node that is no range.
static muiRangeEntry* EntryOf(const muiContext* context, uint32_t slot)
{
    const muiRangeStore* store = &context->ranges;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiRangeEntry* entry = &store->entries[i];
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
    muiRangeStore* store = &context->ranges;
    for (uint32_t i = store->count; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->entries[i - 1].node) == 0)
        {
            store->entries[i - 1] = store->entries[--store->count];
        }
    }
}

// A value within a range and on a step: the minimum plus whole steps,
// the last at or below the maximum, as HTML's range input.
static float Snap(const muiValueRange* range, float value)
{
    value = fminf(fmaxf(value, range->minimum), range->maximum);
    if (range->step > 0.0f)
    {
        float steps = roundf((value - range->minimum) / range->step);
        value = range->minimum + steps * range->step;
        if (value > range->maximum)
        {
            value -= range->step;
        }
    }
    return value;
}

static bool IsValid(const muiValueRange* range)
{
    return isfinite(range->minimum) && isfinite(range->maximum) && isfinite(range->value) &&
           range->maximum >= range->minimum && isfinite(range->step) && range->step >= 0.0f &&
           isfinite(range->page) && range->page >= 0.0f && range->axis <= mui_rangeVertical;
}

muiValueRange muiDefaultValueRange(void)
{
    return (muiValueRange){.maximum = 100.0f, .step = 1.0f, .page = 10.0f};
}

muiResult muiNode_SetValueRange(muiContext* context, muiNodeId nodeId, const muiValueRange* range)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (range == nullptr || !IsValid(range))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (!IsNull(range->thumb) && muiTreeResolve(&context->tree, range->thumb) == 0)
    {
        return mui_errorStale;
    }
    muiRangeStore* store = &context->ranges;
    muiRangeEntry* entry = EntryOf(context, slot);
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
    *entry = (muiRangeEntry){.node = muiTreeIdOf(&context->tree, slot), .range = *range};
    entry->range.value = Snap(range, range->value);
    muiNoteAccess(context, slot);
    return mui_success;
}

// The entry of a node an argument names, with the status to return when
// there is none.
static muiRangeEntry* Find(const muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (IsNull(nodeId))
    {
        *statusOut = mui_errorInvalid;
        return nullptr;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    muiRangeEntry* entry = slot != 0 ? EntryOf(context, slot) : nullptr;
    *statusOut = slot == 0 ? mui_errorStale : entry == nullptr ? mui_empty : mui_success;
    return entry;
}

muiResult muiNode_GetValueRange(const muiContext* context, muiNodeId nodeId,
                                muiValueRange* rangeOut)
{
    if (context == nullptr || rangeOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    const muiRangeEntry* entry = Find(context, nodeId, &status);
    if (entry != nullptr)
    {
        *rangeOut = entry->range;
    }
    return status;
}

muiResult muiNode_SetRangeValue(muiContext* context, muiNodeId nodeId, float value)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!isfinite(value) || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    muiRangeEntry* entry = Find(context, nodeId, &status);
    if (status == mui_errorInvalid)
    {
        return muiRefuse(context);
    }
    if (entry != nullptr)
    {
        entry->range.value = Snap(&entry->range, value);
        muiNoteAccess(context, entry->node.index1);
    }
    return status;
}

muiResult muiNode_ClearValueRange(muiContext* context, muiNodeId nodeId)
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
    muiRangeEntry* entry = Find(context, nodeId, &status);
    if (status == mui_errorInvalid)
    {
        return muiRefuse(context);
    }
    if (entry != nullptr)
    {
        muiNoteAccess(context, entry->node.index1);
        *entry = context->ranges.entries[--context->ranges.count];
    }
    return status == mui_empty ? mui_success : status;
}

// Moves a range's value for input, on a step, reporting a change;
// whether it moved.
static bool Change(muiContext* context, muiRangeEntry* entry, float value)
{
    value = Snap(&entry->range, value);
    if (value == entry->range.value)
    {
        return false;
    }
    entry->range.value = value;
    muiNoteAccess(context, entry->node.index1);
    const muiNotification record = {mui_notificationRangeChanged, entry->node, 0};
    muiNotifyPost(&context->notifications, &record);
    return true;
}

// The step keys move a range by: its step, or a hundredth of it.
static float SmallStep(const muiValueRange* range)
{
    return range->step > 0.0f ? range->step : (range->maximum - range->minimum) / 100.0f;
}

const muiValueRange* muiRangeOf(const muiContext* context, uint32_t slot)
{
    const muiRangeEntry* entry = EntryOf(context, slot);
    return entry != nullptr ? &entry->range : nullptr;
}

bool muiRangeStep(muiContext* context, uint32_t slot, bool up)
{
    muiRangeEntry* entry = EntryOf(context, slot);
    float step = SmallStep(&entry->range);
    return Change(context, entry, entry->range.value + (up ? step : -step));
}

bool muiRangeSet(muiContext* context, uint32_t slot, float value)
{
    return Change(context, EntryOf(context, slot), value);
}

bool muiRangeKey(muiContext* context, uint32_t slot, muiKeyCode code)
{
    muiRangeEntry* entry = EntryOf(context, slot);
    if (entry == nullptr)
    {
        return false;
    }
    const muiValueRange* range = &entry->range;
    float small = SmallStep(range);
    // Right to left, the minimum is on the right.
    float right = context->layout[slot - 1].rtl ? -small : small;
    bool horizontal = range->axis == mui_rangeHorizontal;
    float value = range->value;
    switch (code)
    {
    case mui_codeArrowRight:
    case mui_codeArrowLeft:
        if (!horizontal)
        {
            return false;
        }
        value += code == mui_codeArrowRight ? right : -right;
        break;
    case mui_codeArrowUp:
    case mui_codeArrowDown:
        if (horizontal)
        {
            return false;
        }
        value += code == mui_codeArrowUp ? small : -small;
        break;
    case mui_codePageUp:
        value += range->page;
        break;
    case mui_codePageDown:
        value -= range->page;
        break;
    case mui_codeHome:
        value = range->minimum;
        break;
    case mui_codeEnd:
        value = range->maximum;
        break;
    default:
        return false;
    }
    Change(context, entry, value);
    return true;
}

bool muiRangeDirection(muiContext* context, uint32_t slot, muiDirection direction)
{
    static const muiKeyCode arrows[] = {mui_codeArrowUp, mui_codeArrowDown, mui_codeArrowLeft,
                                        mui_codeArrowRight};
    return muiRangeKey(context, slot, arrows[direction]);
}

// A range's track along its axis, its content box: from its left or top
// edge, with the thumb's place and length there (the value's place and
// 0 without a thumb).
typedef struct Track
{
    float start;
    float end;
    float thumb;
    float length;
} Track;

// How far a point at node's x, y lies along a range's axis in the
// range's box, node being the range or inside it. (A range does not
// scroll its thumb.)
static float Along(const muiContext* context, uint32_t range, uint32_t node, float x, float y,
                   bool horizontal)
{
    double along = horizontal ? (double)x : (double)y;
    for (uint32_t at = node; at != range; at = muiTreeAt(&context->tree, at)->links.parent)
    {
        const muiRect* rect = &context->layout[at - 1].rect;
        along += (double)(horizontal ? rect->x : rect->y);
    }
    return (float)along;
}

// The share of the track the value lies at from the left or the top.
static float Share(const muiContext* context, uint32_t slot, const muiValueRange* range)
{
    float span = range->maximum - range->minimum;
    float share = span > 0.0f ? (range->value - range->minimum) / span : 0.0f;
    bool forward = range->axis == mui_rangeHorizontal && !context->layout[slot - 1].rtl;
    return forward ? share : 1.0f - share;
}

static Track TrackOf(const muiContext* context, uint32_t slot, const muiValueRange* range)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiLayoutStyle* style = &layout->style;
    const muiEdges* padding = &context->paddings[slot - 1];
    bool horizontal = range->axis == mui_rangeHorizontal;
    Track track = {0};
    if (horizontal)
    {
        float left =
            layout->rtl ? style->border.end + padding->end : style->border.start + padding->start;
        float right =
            layout->rtl ? style->border.start + padding->start : style->border.end + padding->end;
        track.start = left;
        track.end = layout->rect.width - right;
    }
    else
    {
        track.start = style->border.top + padding->top;
        track.end = layout->rect.height - style->border.bottom - padding->bottom;
    }
    uint32_t thumb = muiTreeResolve(&context->tree, range->thumb);
    if (thumb != 0 && thumb != slot && muiTreeIsAncestor(&context->tree, slot, thumb))
    {
        const muiRect* rect = &context->layout[thumb - 1].rect;
        track.thumb = Along(context, slot, thumb, 0.0f, 0.0f, horizontal);
        track.length = horizontal ? rect->width : rect->height;
    }
    else
    {
        track.thumb = track.start + Share(context, slot, range) * (track.end - track.start);
    }
    return track;
}

// A press on a range's track pages its value toward the point; one on
// its thumb leaves it for a drag.
static void Press(muiContext* context, muiRangeEntry* entry, uint32_t slot, uint32_t node,
                  const muiPointerRecord* record)
{
    const muiValueRange* range = &entry->range;
    bool horizontal = range->axis == mui_rangeHorizontal;
    const Track track = TrackOf(context, slot, range);
    float point = Along(context, slot, node, record->x, record->y, horizontal);
    float center = track.thumb + track.length / 2.0f;
    if (point >= track.thumb && point < track.thumb + track.length)
    {
        return;
    }
    // Toward larger values: rightward, leftward right to left, upward.
    bool larger = point > center;
    if (!horizontal || context->layout[slot - 1].rtl)
    {
        larger = !larger;
    }
    Change(context, entry, range->value + (larger ? range->page : -range->page));
}

// A drag of a range moves its value with the pointer, keeping where it
// holds the thumb.
static void Follow(muiContext* context, muiRangeEntry* entry, uint32_t slot, float point,
                   const muiPointerRecord* record)
{
    const muiValueRange* range = &entry->range;
    bool horizontal = range->axis == mui_rangeHorizontal;
    const Track track = TrackOf(context, slot, range);
    if (record->kind == mui_pointerRecordDragStart)
    {
        // Where the press began, against the thumb then.
        float pressed = point - (horizontal ? record->offsetX : record->offsetY);
        bool onThumb = pressed >= track.thumb && pressed < track.thumb + track.length;
        entry->grab = onThumb ? pressed - track.thumb : track.length / 2.0f;
        entry->startValue = range->value;
    }
    // A thumb filling the track leaves no travel; past either end the
    // value stops at it.
    float travel = track.end - track.start - track.length;
    if (travel <= 0.0f)
    {
        return;
    }
    float share = (point - entry->grab - track.start) / travel;
    if (!horizontal || context->layout[slot - 1].rtl)
    {
        share = 1.0f - share;
    }
    Change(context, entry, range->minimum + share * (range->maximum - range->minimum));
}

bool muiRangePointer(muiContext* context, const muiPointerRecord* record)
{
    uint32_t node = muiTreeResolve(&context->tree, record->node);
    bool drag = record->kind == mui_pointerRecordDragStart ||
                record->kind == mui_pointerRecordDragMove ||
                record->kind == mui_pointerRecordDragEnd;
    if (node == 0 || (!drag && record->kind != mui_pointerRecordPress))
    {
        return false;
    }
    // The nearest range from the node up: a thumb's label, or a thumb
    // that takes drags itself, reaches its range.
    uint32_t slot = node;
    muiRangeEntry* entry = EntryOf(context, slot);
    while (entry == nullptr && (slot = muiTreeAt(&context->tree, slot)->links.parent) != 0)
    {
        entry = EntryOf(context, slot);
    }
    if (entry == nullptr)
    {
        return false;
    }
    if (record->kind == mui_pointerRecordPress)
    {
        Press(context, entry, slot, node, record);
    }
    else if (record->kind != mui_pointerRecordDragEnd)
    {
        bool horizontal = entry->range.axis == mui_rangeHorizontal;
        Follow(context, entry, slot, Along(context, slot, node, record->x, record->y, horizontal),
               record);
    }
    else if (record->cancelled)
    {
        Change(context, entry, entry->startValue);
    }
    return true;
}
