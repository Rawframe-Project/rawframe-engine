// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "notify.h"

#include "invariant.h"

void muiNotifyInit(muiNotifyQueue* queue, muiNotification* records, uint32_t capacity)
{
    *queue = (muiNotifyQueue){.records = records, .capacity = capacity};
}

void muiNotifyPost(muiNotifyQueue* queue, const muiNotification* record)
{
    // Once one is lost, later ones are too, so the count keeps its place.
    if (queue->dropped != 0 || queue->count == queue->capacity)
    {
        queue->dropped++;
        return;
    }
    // The head stays below the capacity, which is not 0 once a record
    // fits, so the sum stays below 2 * capacity and does not wrap.
    MUI_ASSERT(queue->count < queue->capacity && queue->head < queue->capacity);
    uint32_t tail = (uint32_t)(((uint64_t)queue->head + queue->count) % queue->capacity);
    queue->records[tail] = *record;
    queue->count++;
}

bool muiNotifyTake(muiNotifyQueue* queue, muiNotification* recordOut)
{
    if (queue->count != 0)
    {
        MUI_ASSERT(queue->head < queue->capacity && queue->count <= queue->capacity);
        *recordOut = queue->records[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        return true;
    }
    if (queue->dropped != 0)
    {
        *recordOut = (muiNotification){
            .kind = mui_notificationDropped,
            .notificationsDropped = queue->dropped,
        };
        queue->dropped = 0;
        return true;
    }
    return false;
}
