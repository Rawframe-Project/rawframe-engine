// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Events: the bounded queue.
// Records are kept in the order they are made (creation order; a clip's
// player's in clip order, then start, iteration, end, by src/sweep.c) and
// sorted, stably, by time once the update is done.

#include "maul-tween/event.h"

#include "context_internal.h"
#include "fpenv.h"
#include "invariant.h"

void mtwnPushEvent(mtwnContext* context, const mtwnEvent* event, double along)
{
    uint32_t capacity = context->def.eventCapacity;
    if (capacity == 0)
    {
        // A context of no records makes none, and drops none.
        return;
    }
    if (context->eventCount == capacity)
    {
        ++context->droppedEvents;
        ++context->droppedSinceRead;
        return;
    }
    MTWN_ASSERT(context->eventCount < capacity && context->eventHead < capacity);
    uint32_t at = (context->eventHead + context->eventCount) % capacity;
    context->events[at] = *event;
    context->eventAlong[at] = along;
    ++context->eventCount;
}

void mtwnWorkEvent(mtwnContext* context, mtwnWork* work, const mtwnEvent* event, double along)
{
    if (work->records == NULL)
    {
        mtwnPushEvent(context, event, along);
    }
    else if (work->recordCount < work->recordCapacity)
    {
        work->records[work->recordCount] = *event;
        work->recordAlong[work->recordCount] = along;
        ++work->recordCount;
    }
    else if (work->recordCapacity == context->def.eventCapacity)
    {
        // As many records came before it in this update: the queue is full
        // when it would be pushed.
        ++work->dropped;
    }
    else
    {
        work->overflow = true;
    }
}

static uint32_t SlotOf(const mtwnContext* context, uint32_t i)
{
    return (context->eventHead + i) % context->def.eventCapacity;
}

// Whether a record comes before another: by time, then, for one player,
// by its place along the sweep. Records are made in the players' creation
// order, which the stable sort keeps among players.
static bool Before(const mtwnEvent* a, double alongA, const mtwnEvent* b, double alongB)
{
    if (a->hostTime != b->hostTime)
    {
        return a->hostTime < b->hostTime;
    }
    return a->player.index1 == b->player.index1 && alongA < alongB;
}

static mtwnEvent* EventAt(mtwnContext* context, uint32_t i)
{
    return &context->events[(context->eventHead + i) % context->def.eventCapacity];
}

void mtwnSortEvents(mtwnContext* context, uint32_t count)
{
    // Insertion, stable: records of one place keep the order they were made.
    double* orders = context->eventAlong;
    for (uint32_t i = count + 1; i < context->eventCount; ++i)
    {
        mtwnEvent moving = *EventAt(context, i);
        double order = orders[SlotOf(context, i)];
        uint32_t j = i;
        while (j > count &&
               Before(&moving, order, EventAt(context, j - 1), orders[SlotOf(context, j - 1)]))
        {
            *EventAt(context, j) = *EventAt(context, j - 1);
            orders[SlotOf(context, j)] = orders[SlotOf(context, j - 1)];
            --j;
        }
        *EventAt(context, j) = moving;
        orders[SlotOf(context, j)] = order;
    }
}

void mtwnPlayerEvent(mtwnContext* context, uint32_t player, mtwnEventKind kind, double hostTime,
                     double clipTime)
{
    const mtwnPlayerSlot* slot = mtwnPlayerAt(context, player);
    mtwnEvent event = {0};
    event.hostTime = hostTime;
    event.clipTime = clipTime;
    event.count = 1;
    event.player = mtwnPlayerIdAt(context, player);
    event.node = slot->type == MTWN_PLAYER_CLIP ? MTWN_ROOT_NODE : 0;
    event.kind = kind;
    event.backwards = slot->type == MTWN_PLAYER_CLIP && slot->play.rate < 0.0;
    mtwnPushEvent(context, &event, 0.0);
}

static mtwnResult RunReadEvents(mtwnContext* context, mtwnEvent* eventsOut, uint32_t capacity,
                                uint32_t* countOut, uint64_t* droppedOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (countOut == NULL || (eventsOut == NULL && capacity != 0))
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    uint32_t n = capacity < context->eventCount ? capacity : context->eventCount;
    for (uint32_t i = 0; i < n; ++i)
    {
        eventsOut[i] = *EventAt(context, i);
    }
    if (n > 0)
    {
        context->eventHead = (context->eventHead + n) % context->def.eventCapacity;
        context->eventCount -= n;
    }
    *countOut = n;
    if (droppedOut != NULL)
    {
        *droppedOut = context->droppedSinceRead;
    }
    context->droppedSinceRead = 0;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnContext_ReadEventsWork(mtwnContext* context,
                                                           mtwnEvent* eventsOut, uint32_t capacity,
                                                           uint32_t* countOut, uint64_t* droppedOut)
{
    // Cleared first, so a busy or missing context's failure clears them too.
    if (countOut != NULL)
    {
        *countOut = 0;
    }
    if (droppedOut != NULL)
    {
        *droppedOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunReadEvents(context, eventsOut, capacity, countOut, droppedOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_ReadEvents(mtwnContext* context, mtwnEvent* eventsOut, uint32_t capacity,
                                  uint32_t* countOut, uint64_t* droppedOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result =
        mtwnContext_ReadEventsWork(context, eventsOut, capacity, countOut, droppedOut);
    mtwnFloatEnvLeave(env);
    return result;
}

uint32_t mtwnContext_GetEventCount(const mtwnContext* context)
{
    return context != NULL ? context->eventCount : 0;
}

uint64_t mtwnContext_GetDroppedEvents(const mtwnContext* context)
{
    return context != NULL ? context->droppedEvents : 0;
}
