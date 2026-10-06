// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pointer input (record mui-0007). Each pointer counts itself in the
// hover count of every node from its hovered node up, and while it holds
// a button in the press count of every node from its pressed node up; a
// count leaving or reaching 0 restyles its node. Tree edits take the
// counts of the pointers below the edited node off and put them back.

#include "maul-ui/pointer.h"

#include "context.h"
#include "focus.h"
#include "pointer.h"
#include "popup.h"
#include "scroll.h"
#include "tree.h"

#include "maul-ui/interaction.h"

#include <math.h>

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

// The slot of a node that lives, or 0.
static uint32_t SlotOf(const muiContext* context, muiNodeId nodeId)
{
    return IsNull(nodeId) ? 0 : muiTreeResolve(&context->tree, nodeId);
}

// Adds delta to the hover or press count of node and its ancestors;
// a node whose count leaves or reaches 0 is styled again.
static void Count(muiContext* context, uint32_t node, bool press, int delta)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = node; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        muiNodeStyle* style = &context->style.nodes[at - 1];
        uint8_t* count = press ? &style->presses : &style->hovers;
        bool was = *count != 0;
        *count = (uint8_t)(*count + delta);
        if (was != (*count != 0))
        {
            style->edited = true;
            muiTreeMark(tree, at, mui_stageStyle);
        }
    }
}

static void Post(muiPointerStore* store, const muiPointerRecord* record)
{
    // Once one is lost, later ones are too, so the count keeps its place.
    if (store->dropped != 0 || store->recordCount == store->recordCapacity)
    {
        store->dropped++;
        return;
    }
    // The sum stays below 2 * capacity, so it does not wrap.
    uint32_t tail =
        (uint32_t)(((uint64_t)store->head + store->recordCount) % store->recordCapacity);
    store->records[tail] = *record;
    store->recordCount++;
}

// What an event does, where: the node it goes to and the point there.
typedef struct Target
{
    uint32_t root;
    uint32_t slot;
    float x;
    float y;
    bool passThrough;
} Target;

// The node a record about pointer goes to, which need not be the hit.
static Target TargetAt(const muiContext* context, const Target* hit, const muiPointer* pointer,
                       uint32_t slot)
{
    if (slot == hit->slot)
    {
        return *hit;
    }
    Target target = {hit->root, slot, pointer->x, pointer->y, true};
    if (slot != 0)
    {
        double x = 0.0;
        double y = 0.0;
        muiScrollOriginOf(context, hit->root, slot, &x, &y);
        target.x = (float)((double)pointer->x - x);
        target.y = (float)((double)pointer->y - y);
        target.passThrough = context->interaction[slot - 1].passThrough;
    }
    return target;
}

static void PostAt(muiContext* context, const muiPointer* pointer, muiPointerRecordKind kind,
                   const Target* target, uint8_t button, uint32_t clickCount, uint64_t timeNs)
{
    const muiPointerRecord record = {
        .kind = kind,
        .pointerKind = pointer->kind,
        .button = button,
        .buttons = pointer->buttons,
        .passThrough = target->passThrough,
        .pointer = pointer->id,
        .clickCount = clickCount,
        .node = target->slot != 0 ? muiTreeIdOf(&context->tree, target->slot) : (muiNodeId){0, 0},
        .x = target->x,
        .y = target->y,
        .timeNs = timeNs,
    };
    Post(&context->pointers, &record);
}

// Moves a pointer's hover to the chain of node, 0 for none.
static void Hover(muiContext* context, muiPointer* pointer, uint32_t node)
{
    uint32_t was = SlotOf(context, pointer->hovered);
    if (was == node)
    {
        return;
    }
    if (was != 0)
    {
        Count(context, was, false, -1);
    }
    if (node != 0)
    {
        Count(context, node, false, 1);
    }
    pointer->hovered = node != 0 ? muiTreeIdOf(&context->tree, node) : (muiNodeId){0, 0};
}

// Ends a pointer's press: its chain is no longer pressed.
static void Unpress(muiContext* context, muiPointer* pointer)
{
    uint32_t pressed = SlotOf(context, pointer->pressed);
    if (pressed != 0)
    {
        Count(context, pressed, true, -1);
    }
    pointer->pressed = (muiNodeId){0, 0};
}

// Posts a record about a pointer's drag or what it offers to node, its
// point in node's box; a node destroyed is still named, its point on the
// surface.
static void PostDrag(muiContext* context, const muiPointer* pointer, muiPointerRecordKind kind,
                     muiNodeId node, bool cancelled)
{
    uint32_t slot = SlotOf(context, node);
    uint32_t root = SlotOf(context, pointer->root);
    double x = 0.0;
    double y = 0.0;
    if (slot != 0 && root != 0)
    {
        muiScrollOriginOf(context, root, slot, &x, &y);
    }
    const muiPointerRecord record = {
        .kind = kind,
        .pointerKind = pointer->kind,
        .buttons = pointer->buttons,
        .passThrough = slot == 0 || context->interaction[slot - 1].passThrough,
        .pointer = pointer->id,
        .node = node,
        .x = (float)((double)pointer->x - x),
        .y = (float)((double)pointer->y - y),
        .timeNs = pointer->timeNs,
        .offsetX = pointer->x - pointer->pressX,
        .offsetY = pointer->y - pointer->pressY,
        .cancelled = cancelled,
        .dropKind = pointer->offerKind,
        .dropKey = pointer->offerKey,
    };
    Post(&context->pointers, &record);
}

// Moves the target of a pointer's offer to the node under it that takes
// its kind, posting the leave of the old and the enter of the new.
static void Retarget(muiContext* context, muiPointer* pointer)
{
    // A root gone leaves the hit empty.
    muiHit hit = {0};
    (void)muiHitTest(context, pointer->root, pointer->x, pointer->y, &hit);
    uint32_t at = SlotOf(context, hit.node);
    while (at != 0 && (context->interaction[at - 1].accepts & pointer->offerKind) == 0)
    {
        at = muiTreeAt(&context->tree, at)->links.parent;
    }
    muiNodeId target = at != 0 ? muiTreeIdOf(&context->tree, at) : (muiNodeId){0, 0};
    if (target.index1 == pointer->target.index1 && target.generation == pointer->target.generation)
    {
        return;
    }
    if (!IsNull(pointer->target))
    {
        PostDrag(context, pointer, mui_pointerRecordDropLeave, pointer->target, false);
    }
    if (at != 0)
    {
        PostDrag(context, pointer, mui_pointerRecordDropEnter, target, false);
    }
    pointer->target = target;
}

// Ends a pointer's drag, if one is going: what it offers drops on its
// target, or leaves it when cancelled or the target is gone.
static void EndDrag(muiContext* context, muiPointer* pointer, bool cancelled)
{
    if (!pointer->dragging)
    {
        return;
    }
    if (!IsNull(pointer->target))
    {
        bool drop = !cancelled && SlotOf(context, pointer->target) != 0;
        PostDrag(context, pointer, drop ? mui_pointerRecordDrop : mui_pointerRecordDropLeave,
                 pointer->target, false);
    }
    PostDrag(context, pointer, mui_pointerRecordDragEnd, pointer->dragged, cancelled);
    pointer->dragging = false;
    pointer->offerKind = 0;
    pointer->offerKey = 0;
    pointer->target = (muiNodeId){0, 0};
}

// Ends a pointer's capture, with a record for the node that had it; a
// drag the node had ends cancelled.
static void Uncapture(muiContext* context, muiPointer* pointer)
{
    uint32_t captured = SlotOf(context, pointer->captured);
    if (IsNull(pointer->captured))
    {
        return;
    }
    EndDrag(context, pointer, true);
    // A node destroyed is still named, so the host can tell whose it was.
    const muiPointerRecord record = {
        .kind = mui_pointerRecordCaptureLost,
        .pointerKind = pointer->kind,
        .buttons = pointer->buttons,
        .passThrough = captured == 0 || context->interaction[captured - 1].passThrough,
        .pointer = pointer->id,
        .node = pointer->captured,
        .x = pointer->x,
        .y = pointer->y,
        .timeNs = pointer->timeNs,
    };
    Post(&context->pointers, &record);
    pointer->captured = (muiNodeId){0, 0};
}

static muiPointer* Find(muiPointerStore* store, uint32_t id)
{
    for (uint32_t i = 0; i < store->count; i++)
    {
        if (store->pointers[i].id == id)
        {
            return &store->pointers[i];
        }
    }
    return nullptr;
}

// Lets a pointer go: its counts first, then its place.
static void Forget(muiContext* context, muiPointer* pointer)
{
    Hover(context, pointer, 0);
    Unpress(context, pointer);
    muiPointerStore* store = &context->pointers;
    *pointer = store->pointers[--store->count];
}

// The nearest node that holds both a and b, themselves included; 0 when
// they are in different trees or either is 0.
static uint32_t CommonAncestor(const muiTree* tree, uint32_t a, uint32_t b)
{
    if (b == 0)
    {
        return 0;
    }
    for (uint32_t at = a; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        if (muiTreeIsAncestor(tree, at, b))
        {
            return at;
        }
    }
    return 0;
}

// The click count of a press: one more than the last press's when it is
// the same kind of pointer and button, soon enough and near enough.
static uint32_t CountPress(muiPointerStore* store, const muiPointerEvent* event)
{
    muiClickSeries* series = &store->series;
    // A series never started has count 0, so continuing it gives 1.
    bool continues = series->kind == event->kind && series->button == event->button &&
                     // Time going back wraps past any interval.
                     event->timeNs - series->timeNs <= store->clickIntervalNs &&
                     fabsf(event->x - series->x) <= store->clickDistance &&
                     fabsf(event->y - series->y) <= store->clickDistance;
    *series = (muiClickSeries){event->timeNs, event->x,      event->y,
                               event->kind,   event->button, continues ? series->count + 1 : 1};
    return series->count;
}

static bool IsValidEvent(const muiPointerEvent* event)
{
    return event->kind <= mui_pointerPen && event->action <= mui_pointerLeave &&
           event->player < MUI_MAX_PLAYERS && event->button < 8 && isfinite(event->x) &&
           isfinite(event->y);
}

// The node a pointer hovers after an event: its capture, nothing for a
// touch out of contact, or else the hit.
static uint32_t HoverTarget(const muiContext* context, const muiPointer* pointer, const Target* hit)
{
    uint32_t captured = SlotOf(context, pointer->captured);
    if (captured != 0)
    {
        return captured;
    }
    return pointer->kind == mui_pointerTouch && pointer->buttons == 0 ? 0 : hit->slot;
}

static void Press(muiContext* context, muiPointer* pointer, const Target* hit,
                  const muiPointerEvent* event, muiPointerButtons before)
{
    uint32_t count = CountPress(&context->pointers, event);
    uint32_t captured = SlotOf(context, pointer->captured);
    const Target target = TargetAt(context, hit, pointer, captured != 0 ? captured : hit->slot);
    if (before == 0)
    {
        muiPopupPress(context, target.slot);
        muiFocusPress(context, event->player, target.slot);
        muiScrollStopFlings(context, target.slot, event->timeNs);
    }
    if (before == 0)
    {
        // The nearest node from the target up that takes drags, or for
        // touch and pens that scrolls, which pans.
        bool pans = pointer->kind != mui_pointerMouse;
        uint32_t drag = target.slot;
        while (drag != 0 && !context->interaction[drag - 1].drags &&
               !(pans && context->layout[drag - 1].style.scrollAxes != mui_scrollNone))
        {
            drag = muiTreeAt(&context->tree, drag)->links.parent;
        }
        pointer->dragged = drag != 0 ? muiTreeIdOf(&context->tree, drag) : (muiNodeId){0, 0};
        pointer->pressX = event->x;
        pointer->pressY = event->y;
    }
    if (before == 0 && target.slot != 0)
    {
        Count(context, target.slot, true, 1);
        pointer->pressed = muiTreeIdOf(&context->tree, target.slot);
        // A touch or a pen captures to what it pressed.
        if (pointer->kind != mui_pointerMouse && captured == 0)
        {
            pointer->captured = pointer->pressed;
        }
    }
    PostAt(context, pointer, mui_pointerRecordPress, &target, event->button, count, event->timeNs);
}

static void Release(muiContext* context, muiPointer* pointer, const Target* hit,
                    const muiPointerEvent* event)
{
    uint32_t count = context->pointers.series.count;
    uint32_t captured = SlotOf(context, pointer->captured);
    const Target target = TargetAt(context, hit, pointer, captured != 0 ? captured : hit->slot);
    PostAt(context, pointer, mui_pointerRecordRelease, &target, event->button, count,
           event->timeNs);
    // A press that became a drag clicks nothing; its drag ends with its
    // last button.
    if (pointer->dragStarted)
    {
        if (pointer->buttons == 0)
        {
            EndDrag(context, pointer, false);
        }
        return;
    }
    uint32_t pressed = SlotOf(context, pointer->pressed);
    uint32_t clicked =
        captured != 0 ? captured : CommonAncestor(&context->tree, pressed, hit->slot);
    if (clicked != 0)
    {
        const Target click = TargetAt(context, hit, pointer, clicked);
        PostAt(context, pointer, mui_pointerRecordClick, &click, event->button, count,
               event->timeNs);
    }
}

static void Cancel(muiContext* context, muiPointer* pointer, const Target* hit,
                   const muiPointerEvent* event, muiPointerButtons before)
{
    if (before == 0)
    {
        return;
    }
    uint32_t captured = SlotOf(context, pointer->captured);
    uint32_t pressed = SlotOf(context, pointer->pressed);
    const Target target = TargetAt(context, hit, pointer, captured != 0 ? captured : pressed);
    PostAt(context, pointer, mui_pointerRecordCancel, &target, 0, 0, event->timeNs);
    EndDrag(context, pointer, true);
}

// Starts or moves a pointer's drag; whether it posted a record.
static bool Drag(muiContext* context, muiPointer* pointer)
{
    uint32_t slot = SlotOf(context, pointer->dragged);
    if (slot == 0 || (pointer->dragStarted && !pointer->dragging))
    {
        return false;
    }
    if (!pointer->dragging)
    {
        const muiPointerStore* store = &context->pointers;
        float threshold = pointer->kind == mui_pointerMouse ? store->dragMouse : store->dragTouch;
        if (!(fabsf(pointer->x - pointer->pressX) > threshold ||
              fabsf(pointer->y - pointer->pressY) > threshold))
        {
            return false;
        }
        if (SlotOf(context, pointer->captured) != slot)
        {
            Uncapture(context, pointer);
            pointer->captured = pointer->dragged;
        }
        pointer->dragStarted = true;
        pointer->dragging = true;
        PostDrag(context, pointer, mui_pointerRecordDragStart, pointer->dragged, false);
        return true;
    }
    PostDrag(context, pointer, mui_pointerRecordDragMove, pointer->dragged, false);
    if (pointer->offerKind != 0)
    {
        Retarget(context, pointer);
    }
    return true;
}

// After an event: a pointer whose buttons are all up ends its press, drag
// and capture; one gone is forgotten; else it hovers what it is over.
static void Settle(muiContext* context, muiPointer* pointer, const Target* hit,
                   const muiPointerEvent* event)
{
    if (pointer->buttons == 0)
    {
        // A drag whose release went missing ends with its buttons; the
        // next press finds its own node to drag.
        EndDrag(context, pointer, false);
        pointer->dragStarted = false;
        Unpress(context, pointer);
        Uncapture(context, pointer);
    }
    bool gone = event->action == mui_pointerCancel ||
                (pointer->buttons == 0 && SlotOf(context, pointer->captured) == 0 &&
                 (event->action == mui_pointerLeave ||
                  (pointer->kind == mui_pointerTouch && event->action == mui_pointerRelease)));
    if (gone)
    {
        Forget(context, pointer);
        return;
    }
    // A pointer that left hovers nothing, unless captured.
    bool away = event->action == mui_pointerLeave && SlotOf(context, pointer->captured) == 0;
    Hover(context, pointer, away ? 0 : HoverTarget(context, pointer, hit));
}

muiResult muiPointerInput(muiContext* context, muiNodeId rootId, const muiPointerEvent* event)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (event == nullptr || IsNull(rootId) || !IsValidEvent(event) || muiIsInHostCall(context) ||
        context->events.dispatching)
    {
        return muiRefuse(context);
    }
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    muiPointerStore* store = &context->pointers;
    muiPointer* pointer = Find(store, event->pointer);
    if (pointer == nullptr)
    {
        if (event->action == mui_pointerCancel || event->action == mui_pointerLeave)
        {
            return mui_success;
        }
        if (store->count == store->capacity)
        {
            return mui_errorCapacity;
        }
        pointer = &store->pointers[store->count++];
        *pointer = (muiPointer){.id = event->pointer, .kind = event->kind};
    }
    muiPointerButtons before = pointer->buttons;
    pointer->kind = event->kind;
    pointer->buttons = event->buttons;
    pointer->x = event->x;
    pointer->y = event->y;
    pointer->timeNs = event->timeNs;
    pointer->root = rootId;
    muiHit found = {0};
    (void)muiHitTest(context, rootId, event->x, event->y, &found);
    // A point over nothing stays in the root's space.
    uint32_t slot = SlotOf(context, found.node);
    const Target hit = {root, slot, slot != 0 ? found.x : event->x, slot != 0 ? found.y : event->y,
                        found.passThrough};
    switch (event->action)
    {
    case mui_pointerPress:
        Press(context, pointer, &hit, event, before);
        break;
    case mui_pointerRelease:
        Release(context, pointer, &hit, event);
        break;
    case mui_pointerCancel:
        Cancel(context, pointer, &hit, event, before);
        pointer->buttons = 0;
        break;
    default:
        if (event->action == mui_pointerMove && pointer->buttons != 0 && Drag(context, pointer))
        {
            break;
        }
        if (SlotOf(context, pointer->captured) != 0 && event->action == mui_pointerMove)
        {
            const Target target =
                TargetAt(context, &hit, pointer, SlotOf(context, pointer->captured));
            PostAt(context, pointer, mui_pointerRecordMove, &target, 0, 0, event->timeNs);
        }
        break;
    }
    Settle(context, pointer, &hit, event);
    return mui_success;
}

muiResult muiNextPointerRecord(muiContext* context, muiPointerRecord* recordOut)
{
    if (context == nullptr || recordOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPointerStore* store = &context->pointers;
    if (store->recordCount != 0)
    {
        *recordOut = store->records[store->head];
        store->head = (store->head + 1) % store->recordCapacity;
        store->recordCount--;
        return mui_success;
    }
    if (store->dropped != 0)
    {
        *recordOut = (muiPointerRecord){
            .kind = mui_pointerRecordDropped, .passThrough = true, .clickCount = store->dropped};
        store->dropped = 0;
        return mui_success;
    }
    return mui_empty;
}

muiResult muiPointer_SetCapture(muiContext* context, uint32_t pointerId, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPointer* pointer = Find(&context->pointers, pointerId);
    if (pointer == nullptr || pointer->buttons == 0 || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (SlotOf(context, pointer->captured) == slot)
    {
        return mui_success;
    }
    Uncapture(context, pointer);
    pointer->captured = muiTreeIdOf(&context->tree, slot);
    Hover(context, pointer, slot);
    return mui_success;
}

muiResult muiPointer_ReleaseCapture(muiContext* context, uint32_t pointerId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPointer* pointer = Find(&context->pointers, pointerId);
    if (pointer == nullptr || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    Uncapture(context, pointer);
    return mui_success;
}

muiResult muiPointer_GetState(const muiContext* context, uint32_t pointerId,
                              muiPointerState* stateOut)
{
    if (context == nullptr || stateOut == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiPointerStore* store = &context->pointers;
    for (uint32_t i = 0; i < store->count; i++)
    {
        const muiPointer* pointer = &store->pointers[i];
        if (pointer->id == pointerId)
        {
            *stateOut =
                (muiPointerState){pointer->kind,    pointer->buttons, pointer->x,       pointer->y,
                                  pointer->hovered, pointer->pressed, pointer->captured};
            return mui_success;
        }
    }
    return mui_empty;
}

muiResult muiSetClickRule(muiContext* context, uint64_t intervalNs, float distance)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!isfinite(distance) || distance < 0.0f || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    context->pointers.clickIntervalNs = intervalNs;
    context->pointers.clickDistance = distance;
    return mui_success;
}

muiResult muiPointer_Offer(muiContext* context, uint32_t pointerId, uint32_t kind, uint64_t key)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPointer* pointer = Find(&context->pointers, pointerId);
    if (pointer == nullptr || !pointer->dragging || kind == 0 || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    pointer->offerKind = kind;
    pointer->offerKey = key;
    Retarget(context, pointer);
    return mui_success;
}

muiResult muiSetDragThreshold(muiContext* context, float mouse, float touch)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!isfinite(mouse) || mouse < 0.0f || !isfinite(touch) || touch < 0.0f ||
        muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    context->pointers.dragMouse = mouse;
    context->pointers.dragTouch = touch;
    return mui_success;
}

bool muiCancelDrags(muiContext* context)
{
    bool any = false;
    muiPointerStore* store = &context->pointers;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPointer* pointer = &store->pointers[i];
        if (pointer->dragging)
        {
            EndDrag(context, pointer, true);
            Uncapture(context, pointer);
            any = true;
        }
    }
    return any;
}

muiPointersTaken muiTakePointersOff(muiContext* context, uint32_t node)
{
    muiPointersTaken taken = 0;
    const muiTree* tree = &context->tree;
    muiPointerStore* store = &context->pointers;
    for (uint32_t i = 0; i < store->count; i++)
    {
        uint32_t hovered = SlotOf(context, store->pointers[i].hovered);
        if (hovered != 0 && muiTreeIsAncestor(tree, node, hovered))
        {
            Count(context, hovered, false, -1);
            taken |= 1ull << i;
        }
        uint32_t pressed = SlotOf(context, store->pointers[i].pressed);
        if (pressed != 0 && muiTreeIsAncestor(tree, node, pressed))
        {
            Count(context, pressed, true, -1);
            taken |= 1ull << (i + MUI_MAX_POINTERS);
        }
    }
    return taken;
}

void muiPutPointersOn(muiContext* context, muiPointersTaken taken)
{
    muiPointerStore* store = &context->pointers;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiPointer* pointer = &store->pointers[i];
        if ((taken >> i & 1u) != 0)
        {
            uint32_t hovered = SlotOf(context, pointer->hovered);
            if (hovered != 0)
            {
                Count(context, hovered, false, 1);
            }
            else
            {
                pointer->hovered = (muiNodeId){0, 0};
            }
            if (!IsNull(pointer->captured) && SlotOf(context, pointer->captured) == 0)
            {
                Uncapture(context, pointer);
            }
        }
        if ((taken >> (i + MUI_MAX_POINTERS) & 1u) != 0)
        {
            uint32_t pressed = SlotOf(context, pointer->pressed);
            if (pressed != 0)
            {
                Count(context, pressed, true, 1);
            }
            else
            {
                pointer->pressed = (muiNodeId){0, 0};
            }
        }
    }
}
