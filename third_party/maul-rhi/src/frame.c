// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Frames: one open at a time on a device, submitted to its driver with a
// token, and answered once in the device's notification queue when the
// GPU finishes. Taking in finished frames polls the driver; nothing here
// blocks unless the program waits.

#include "device_core.h"
#include "invariant.h"

#include <stdatomic.h>
#include <string.h>

#define FRAME_DEF_COOKIE 0x6D726672u

// Driver events a poll moves at a time.
#define POLL_BATCH 8

mrhiFrameDef mrhiDefaultFrameDef(void)
{
    mrhiFrameDef def = {0};
    def.cookie = FRAME_DEF_COOKIE;
    return def;
}

uint32_t mrhiAnswerRoom(const mrhiDevice* device)
{
    // One record is kept for the loss notice until it is queued.
    uint64_t kept = device->state == mrhi_deviceLost ? 0 : 1;
    uint64_t used = (uint64_t)device->queueCount + device->runningCount + device->pendingCount +
                    device->readbackPending + kept;
    // Every answer is reserved before it is owed, so none overfills it.
    MRHI_ASSERT(used <= device->deviceLimits.notifications);
    return (uint32_t)(device->deviceLimits.notifications - used);
}

bool mrhiHasAnswerRoom(const mrhiDevice* device)
{
    return mrhiAnswerRoom(device) > 0;
}

void mrhiQueueAnswer(mrhiDevice* device, mrhiDeviceNotificationKind kind, uint32_t request,
                     mrhiResult outcome)
{
    uint32_t tail = (device->queueHead + device->queueCount) % device->deviceLimits.notifications;
    device->queue[tail] = (mrhiDeviceNotification){
        .kind = kind,
        // Request 0, a loss notice's, is the null id.
        .request = {request, request != 0},
        .outcome = outcome,
    };
    ++device->queueCount;
}

// A staging region no running frame uses. Running frames hold distinct
// regions, so one of the first runningCount + 1 is free, whatever order
// frames finish in.
static uint32_t FreeRegion(const mrhiDevice* device)
{
    for (uint32_t region = 0;; ++region)
    {
        bool used = false;
        for (uint32_t i = 0; i < device->runningCount; ++i)
        {
            used = used || device->runningRegions[i] == region;
        }
        if (!used)
        {
            return region;
        }
    }
}

// Takes a finished frame off the running list and queues its answer;
// submission made room for it.
static void Finish(mrhiDevice* device, uint64_t tag, mrhiResult outcome)
{
    for (uint32_t i = 0; i < device->runningCount; ++i)
    {
        if (device->running[i] == tag)
        {
            device->lastFinished = (uint32_t)tag;
            mrhiQueueAnswer(device, mrhi_deviceFrameDone, (uint32_t)tag, outcome);
            mrhiAnswerReadbacks(device, device->runningReadbackFirst[i],
                                device->runningReadbackCount[i], outcome);
            uint32_t last = --device->runningCount;
            device->running[i] = device->running[last];
            device->runningRegions[i] = device->runningRegions[last];
            device->runningReadbackFirst[i] = device->runningReadbackFirst[last];
            device->runningReadbackCount[i] = device->runningReadbackCount[last];
            return;
        }
    }
}

// Loses the device with a report: the driver's, or the one given.
static void Lose(mrhiDevice* device, const mrhiDeviceLossReport* given)
{
    if (device->state == mrhi_deviceLost)
    {
        return;
    }
    device->state = mrhi_deviceLost;
    mrhiDeviceLossReport report = {0};
    if (given != nullptr)
    {
        report = *given;
    }
    else
    {
        device->driver.vtable->lossReport(device->driver.self, &report);
    }
    report.lastSubmitted = (mrhiRequestId){device->lastSubmitted, device->lastSubmitted != 0};
    report.lastFinished = (mrhiRequestId){device->lastFinished, device->lastFinished != 0};
    MRHI_ASSERT(report.messageLength <= MRHI_LOSS_MESSAGE_BYTES);
    device->lossReport = report;
    // The notice's record was kept free, and every other answer reserved.
    mrhiQueueAnswer(device, mrhi_deviceLostNotice, 0, mrhi_errorDeviceLost);
    while (device->runningCount > 0)
    {
        Finish(device, device->running[0], mrhi_errorDeviceLost);
    }
    mrhiLosePipelines(device);
}

void mrhiLoseDevice(mrhiDevice* device)
{
    Lose(device, nullptr);
}

mrhiResult mrhiSimulateDeviceLoss(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (device->state != mrhi_deviceReady)
    {
        return device->state == mrhi_deviceLost ? mrhi_errorDeviceLost : mrhi_errorState;
    }
    static const char message[] = "Lost through mrhiSimulateDeviceLoss";
    mrhiDeviceLossReport report = {
        .reason = mrhi_lossSimulated,
        .messageLength = sizeof(message) - 1,
    };
    memcpy(report.message, message, sizeof(message) - 1);
    Lose(device, &report);
    return mrhi_success;
}

mrhiResult mrhiDriverStatus(mrhiDevice* device, mrhiResult status)
{
    if (status == mrhi_errorDeviceLost)
    {
        mrhiLoseDevice(device);
    }
    return status;
}

// Takes in the work the driver has finished: frames, whose tags are
// their tokens, and pipelines, whose tags carry their slot above.
static void TakeFinished(mrhiDevice* device)
{
    mrhiDriverEvent events[POLL_BATCH];
    size_t moved;
    while ((moved = device->driver.vtable->poll(device->driver.self, events, POLL_BATCH)) > 0)
    {
        // A lost device answered everything it owed when it was lost.
        for (size_t i = 0; i < moved && device->state != mrhi_deviceLost; ++i)
        {
            if (events[i].tag == 0)
            {
                mrhiLoseDevice(device);
            }
            else if (events[i].tag > UINT32_MAX)
            {
                mrhiFinishPipeline(device, events[i].tag, events[i].outcome);
            }
            else
            {
                Finish(device, events[i].tag, events[i].outcome);
            }
        }
    }
}

mrhiResult mrhiBeginFrame(mrhiDevice* device, const mrhiFrameDef* def)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (def == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiDefHead head = {def->cookie, def->next, nullptr, 0};
    mrhiResult status = mrhiCheckObjectDef(device, head, FRAME_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    if (device->frameOpen)
    {
        return mrhi_errorState;
    }
    TakeFinished(device);
    if (device->runningCount == device->limits.framesInFlight)
    {
        return mrhiDeviceFull(device, mrhi_diagnosticFramesInFlight);
    }
    device->frameOpen = true;
    device->stagingRegion = FreeRegion(device);
    mrhiMarkReadbacks(device);
    atomic_store_explicit(&device->stagingTaken, 0, memory_order_relaxed);
    atomic_store_explicit(&device->countedTaken, 0, memory_order_relaxed);
    device->frameSerial = device->frameSerial == UINT32_MAX ? 1 : device->frameSerial + 1;
    ++device->frameNumber;
    device->frameCompiled = false;
    device->frameResourceCount = 0;
    device->frameUseCount = 0;
    device->frameBarrierCount = 0;
    device->frameLibraryBarriers = 0;
    atomic_store_explicit(&device->frameChunksTaken, 0, memory_order_relaxed);
    device->frameChunkLimit = device->frameChunkCount;
    mrhiBeginZeroPass(device);
    return mrhi_success;
}

mrhiResult mrhiDropFrame(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (!device->frameOpen)
    {
        return mrhi_errorState;
    }
    device->frameOpen = false;
    mrhiDropReadbacks(device);
    mrhiReleaseImages(device);
    return mrhi_success;
}

mrhiResult mrhiSubmitFrame(mrhiDevice* device, mrhiRequestId* tokenOut)
{
    if (tokenOut != nullptr)
    {
        *tokenOut = (mrhiRequestId){0};
    }
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (tokenOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!device->frameOpen)
    {
        return mrhi_errorState;
    }
    // A frame recorded while its device was lost never runs.
    if (device->state == mrhi_deviceLost)
    {
        mrhiResult dropped = mrhiDropFrame(device);
        return dropped == mrhi_success ? mrhi_errorDeviceLost : dropped;
    }
    mrhiResult compiled = device->frameCompiled ? mrhi_success : mrhiCompile(device);
    if (compiled != mrhi_success)
    {
        return compiled;
    }
    for (uint32_t i = 0; i < device->framePassCount; ++i)
    {
        const mrhiFramePass* pass = &device->framePasses[i];
        if (atomic_load_explicit(&pass->recording, memory_order_acquire) == mrhiRecordingOpen)
        {
            return mrhi_errorState;
        }
        if (pass->overflow != 0)
        {
            return mrhiDeviceFull(device, pass->overflow);
        }
    }
    if (!mrhiHasAnswerRoom(device))
    {
        return mrhiDeviceFull(device, mrhi_diagnosticNotifications);
    }
    mrhiDriverFrame view;
    mrhiViewFrame(device, &view);
    device->frameOpen = false;
    uint32_t token = device->lastRequest + 1;
    mrhiDiagnosticCode budget = 0;
    mrhiResult status = mrhiDriverStatus(
        device, device->driver.vtable->submitFrame(device->driver.self, &view, token, &budget));
    if (status == mrhi_errorCapacity && budget != 0)
    {
        mrhiRecordDiagnostic(&device->diagnostics, budget);
    }
    if (status != mrhi_success)
    {
        mrhiDropReadbacks(device);
        mrhiReleaseImages(device);
        return status;
    }
    mrhiApplyFinalStates(device);
    mrhiMarkZeroed(device);
    device->lastRequest = token;
    device->lastSubmitted = token;
    device->runningRegions[device->runningCount] = device->stagingRegion;
    device->runningReadbackFirst[device->runningCount] = device->frameReadbackFirst;
    device->runningReadbackCount[device->runningCount] =
        device->readbackHead - device->frameReadbackFirst;
    device->running[device->runningCount++] = token;
    *tokenOut = (mrhiRequestId){token, 1};
    return mrhi_success;
}

mrhiResult mrhiGetFrameUsage(mrhiDevice* device, mrhiFrameUsage* usageOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (usageOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    const mrhiDeviceLimits* limits = &device->deviceLimits;
    // Before the first frame no pass is counted, not even the library's.
    // A refused take may have counted past the room, which was then full.
    // Once the frame compiled, the library's own pass had recorded first,
    // into the chunks past the program's.
    uint32_t libraryPasses = device->framePassCount > 0 ? MRHI_LIBRARY_PASSES : 0;
    uint64_t staged = atomic_load_explicit(&device->stagingTaken, memory_order_relaxed);
    uint32_t chunks = atomic_load_explicit(&device->frameChunksTaken, memory_order_relaxed);
    uint32_t libraryChunks =
        device->frameCompiled ? device->frameChunkLimit - device->frameProgramChunks : 0;
    chunks = chunks < device->frameChunkLimit ? chunks : device->frameChunkLimit;
    *usageOut = (mrhiFrameUsage){
        .frameResources = device->frameResourceCount,
        .framePasses = device->framePassCount - libraryPasses,
        .frameAccesses = device->frameUseCount,
        .frameBarriers = device->frameBarrierCount - device->frameLibraryBarriers,
        .frameCommandBytes = (chunks - libraryChunks) * MRHI_CHUNK_BYTES,
        .frameUploadBytes =
            (uint32_t)(staged < limits->frameUploadBytes ? staged : limits->frameUploadBytes),
        .frameIndirectDraws =
            (uint32_t)atomic_load_explicit(&device->countedTaken, memory_order_relaxed),
    };
    return mrhi_success;
}

mrhiResult mrhiWaitFrame(mrhiDevice* device, mrhiRequestId token, uint64_t timeoutNs)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (token.index1 == 0 || token.index1 > device->lastRequest || token.generation != 1)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticFrameToken);
    }
    bool running = false;
    for (uint32_t i = 0; i < device->runningCount; ++i)
    {
        running = running || device->running[i] == token.index1;
    }
    if (!running)
    {
        return mrhi_success;
    }
    // A finished frame is answered at the next poll.
    return device->driver.vtable->waitFrame(device->driver.self, token.index1, timeoutNs)
               ? mrhi_success
               : mrhi_timeout;
}

mrhiResult mrhiNextDeviceNotification(mrhiDevice* device, mrhiDeviceNotification* notificationOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (notificationOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (device->queueCount == 0)
    {
        TakeFinished(device);
    }
    if (device->queueCount == 0)
    {
        return mrhi_empty;
    }
    *notificationOut = device->queue[device->queueHead];
    device->queueHead = (device->queueHead + 1) % device->deviceLimits.notifications;
    --device->queueCount;
    return mrhi_success;
}

mrhiResult mrhiWaitDeviceNotification(mrhiDevice* device, uint64_t timeoutNs)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (device->queueCount == 0)
    {
        TakeFinished(device);
    }
    // The oldest running frame, the smallest token, ends first: its end
    // is the next answer GPU work brings, its readbacks with it. A pending
    // pipeline needs no wait: a native driver makes it at its creation and
    // answers at the poll above, and the web, which never blocks here,
    // settles it in the browser.
    uint32_t oldest = 0;
    for (uint32_t i = 0; i < device->runningCount && device->queueCount == 0; ++i)
    {
        oldest = oldest == 0 || device->running[i] < oldest ? device->running[i] : oldest;
    }
    if (oldest != 0 && device->driver.vtable->waitFrame(device->driver.self, oldest, timeoutNs))
    {
        TakeFinished(device);
    }
    if (device->queueCount > 0)
    {
        return mrhi_success;
    }
    // A readback is owed once its frame is submitted, and its frame then
    // runs; one recorded in the open frame waits for the submit.
    bool owed = device->runningCount > 0 || device->pendingCount > 0;
    return owed ? mrhi_timeout : mrhi_empty;
}
