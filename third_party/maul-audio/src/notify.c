// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The notification queue: a ring of records in the context's block. It
// never grows; its last place is kept for the record that says how
// many were lost.

#include "notify.h"

#include "backend.h"
#include "context.h"
#include "invariant.h"

#include <string.h>

static maudNotification* At(maudNotificationQueue* queue, uint32_t index)
{
    return &queue->records[(queue->head + index) % queue->capacity];
}

void maudPostNotification(maudContext* context, const maudNotification* record)
{
    maudNotificationQueue* queue = &context->notifications;
    MAUD_ASSERT(queue->capacity >= 2);
    if (queue->count + 1 < queue->capacity)
    {
        *At(queue, queue->count) = *record;
        queue->count++;
        return;
    }
    if (queue->count + 1 == queue->capacity)
    {
        *At(queue, queue->count) =
            (maudNotification){.kind = maud_notifyOverflow, .droppedCount = 1};
        queue->count++;
        return;
    }
    maudNotification* overflow = At(queue, queue->count - 1);
    MAUD_ASSERT(overflow->kind == maud_notifyOverflow);
    if (overflow->droppedCount < UINT32_MAX)
    {
        overflow->droppedCount++;
    }
}

maudResult maudContext_NextNotification(maudContext* context, maudNotification* notificationOut)
{
    // Cleared first, so a failure leaves zero.
    if (notificationOut != nullptr)
    {
        memset(notificationOut, 0, sizeof(*notificationOut));
    }
    if (context == nullptr || notificationOut == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudRefuseControl(context))
    {
        return maud_errorBusy;
    }
    if (context->backend->pump != nullptr)
    {
        context->backend->pump(context);
    }
    maudNotificationQueue* queue = &context->notifications;
    if (queue->count == 0)
    {
        return maud_empty;
    }
    *notificationOut = *At(queue, 0);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return maud_success;
}
