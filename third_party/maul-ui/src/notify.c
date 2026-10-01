// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "notify.h"

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
    // The sum stays below 2 * capacity, so it does not wrap.
    uint32_t tail = (uint32_t)(((uint64_t)queue->head + queue->count) % queue->capacity);
    queue->records[tail] = *record;
    queue->count++;
}

bool muiNotifyTake(muiNotifyQueue* queue, muiNotification* recordOut)
{
    if (queue->count != 0)
    {
        *recordOut = queue->records[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;
        return true;
    }
    if (queue->dropped != 0)
    {
        *recordOut = (muiNotification){
            .kind = mui_notificationDropped,
            .count = queue->dropped,
        };
        queue->dropped = 0;
        return true;
    }
    return false;
}
