// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instance's notification queue: pending work, its driver polled
// when the queue is drained, and exactly one record per request.

#include "instance_core.h"
#include "invariant.h"

// Driver events a poll moves at a time.
#define POLL_BATCH 8

bool mrhiHasRoomForAnswer(const mrhiInstance* instance)
{
    return instance->queueCount + instance->pendingCount < instance->limits.notifications;
}

uint32_t mrhiPeekRequest(const mrhiInstance* instance)
{
    uint32_t next = instance->nextRequest + 1;
    return next == 0 ? 1 : next;
}

uint32_t mrhiNextRequest(mrhiInstance* instance)
{
    instance->nextRequest = mrhiPeekRequest(instance);
    return instance->nextRequest;
}

void mrhiPushInstanceNotification(mrhiInstance* instance, mrhiInstanceNotification notification)
{
    // Every request reserved its answer's room when it was accepted.
    MRHI_ASSERT(instance->queueCount < instance->limits.notifications);
    uint32_t tail = (instance->queueHead + instance->queueCount) % instance->limits.notifications;
    instance->queue[tail] = notification;
    ++instance->queueCount;
}

void mrhiAddPending(mrhiInstance* instance, mrhiPending pending)
{
    MRHI_ASSERT(mrhiHasRoomForAnswer(instance) && pending.request != 0);
    instance->pending[instance->pendingCount++] = pending;
}

// Takes a pending request off the list, answers it, and queues the
// record.
static void Answer(mrhiInstance* instance, uint32_t index, mrhiResult outcome)
{
    MRHI_ASSERT(index < instance->pendingCount);
    mrhiPending pending = instance->pending[index];
    instance->pending[index] = instance->pending[--instance->pendingCount];
    mrhiInstanceNotificationKind kind = mrhi_instanceAdaptersFound;
    if (pending.kind == mrhiPendingAdapters)
    {
        outcome = outcome == mrhi_success ? mrhiRefreshAdapters(instance, &pending) : outcome;
    }
    else
    {
        kind = mrhi_instanceDeviceReady;
        outcome = mrhiFinishOpening(pending.device, outcome);
    }
    mrhiPushInstanceNotification(instance, (mrhiInstanceNotification){
                                               .kind = kind,
                                               .request = {pending.request, 1},
                                               .outcome = outcome,
                                           });
}

void mrhiAnswerNow(mrhiInstance* instance, uint32_t request, mrhiResult outcome)
{
    for (uint32_t i = 0; i < instance->pendingCount; ++i)
    {
        if (instance->pending[i].request == request)
        {
            Answer(instance, i, outcome);
            return;
        }
    }
}

static void PollDriver(mrhiInstance* instance)
{
    if (instance->driver.vtable == nullptr)
    {
        return;
    }
    mrhiDriverEvent events[POLL_BATCH];
    size_t moved;
    while ((moved = instance->driver.vtable->poll(instance->driver.self, events, POLL_BATCH)) > 0)
    {
        for (size_t i = 0; i < moved; ++i)
        {
            for (uint32_t p = 0; p < instance->pendingCount; ++p)
            {
                if (instance->pending[p].request == events[i].tag)
                {
                    Answer(instance, p, events[i].outcome);
                    break;
                }
            }
        }
    }
}

mrhiResult mrhiNextInstanceNotification(mrhiInstance* instance,
                                        mrhiInstanceNotification* notificationOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (instance->allocatorGuard.busy)
    {
        return mrhiInstanceBusy(instance);
    }
    if (notificationOut == nullptr)
    {
        return mrhiMisuse(instance, mrhi_diagnosticNullArgument);
    }
    if (instance->queueCount == 0)
    {
        PollDriver(instance);
    }
    if (instance->queueCount == 0)
    {
        return mrhi_empty;
    }
    *notificationOut = instance->queue[instance->queueHead];
    instance->queueHead = (instance->queueHead + 1) % instance->limits.notifications;
    --instance->queueCount;
    return mrhi_success;
}
