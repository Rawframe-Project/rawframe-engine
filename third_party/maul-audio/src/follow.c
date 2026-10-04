// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Device following. The control thread changes where a stream stands;
// the rendering thread sees only the run state and the block rate,
// published through atomics, and picks them up at its next render.

#include "follow.h"

#include "backend.h"
#include "context.h"
#include "notify.h"

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Publishes whether the stream runs, and lets the platform run it or
// holds it when that changes.
static void Publish(maudContext* context, maudStreamSlot* slot)
{
    maudStreamCore* core = &slot->core;
    bool running = core->binding.started && core->binding.suspension == maud_suspendNone;
    uint8_t state = running ? maud_streamRunning : maud_streamIdle;
    uint8_t previous = atomic_exchange_explicit(&core->state, state, memory_order_acq_rel);
    if (previous != state && context->backend->setStreamActive != nullptr)
    {
        context->backend->setStreamActive(context, slot, running);
    }
}

static void Suspend(maudContext* context, maudStreamSlot* slot, maudSuspendReason reason)
{
    maudStreamCore* core = &slot->core;
    core->binding.current = (maudDeviceId){0, 0};
    if (core->binding.suspension == reason)
    {
        return;
    }
    core->binding.suspension = reason;
    Publish(context, slot);
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyStreamSuspended,
                                      .reason = reason,
                                      .streamId = maudStreamIdOf(context, slot),
                                  });
}

// Gives a native stream its device's rate, if that differs from the
// rate it runs at; true when it did.
static bool TakeDeviceRate(maudContext* context, maudStreamSlot* slot)
{
    maudStreamCore* core = &slot->core;
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    if (device == nullptr || core->format.ratePolicy != maud_rateNative ||
        device->info.nativeSampleRate == core->format.sampleRate)
    {
        return false;
    }
    core->format.sampleRate = device->info.nativeSampleRate;
    atomic_store_explicit(&core->blockRate, core->format.sampleRate, memory_order_release);
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyStreamFormatChanged,
                                      .streamId = maudStreamIdOf(context, slot),
                                      .sampleRate = core->format.sampleRate,
                                  });
    return true;
}

static void Retarget(maudContext* context, maudStreamSlot* slot)
{
    if (context->backend->retargetStream != nullptr)
    {
        context->backend->retargetStream(context, slot);
    }
}

// Gives a native stream its device's rate and the platform the new
// format, if the rate changed.
static void ApplyDeviceRate(maudContext* context, maudStreamSlot* slot)
{
    if (TakeDeviceRate(context, slot))
    {
        Retarget(context, slot);
    }
}

// Holds a stream on its device with reason: unlike Suspend, it keeps
// the device, since the stream is not without one.
static void Wait(maudContext* context, maudStreamSlot* slot, maudSuspendReason reason)
{
    maudStreamCore* core = &slot->core;
    if (core->binding.suspension == reason)
    {
        return;
    }
    core->binding.suspension = reason;
    Publish(context, slot);
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyStreamSuspended,
                                      .reason = reason,
                                      .streamId = maudStreamIdOf(context, slot),
                                  });
}

// Ends a stream's suspension: it runs again, or, while the host has
// suspended the context, the platform holds it or has not granted
// access, waits for that instead.
static void Resume(maudContext* context, maudStreamSlot* slot)
{
    if (context->hostSuspended)
    {
        Wait(context, slot, maud_suspendHost);
        return;
    }
    if (context->held)
    {
        Wait(context, slot, maud_suspendPolicy);
        return;
    }
    if (slot->core.binding.awaitingPermission)
    {
        Wait(context, slot, maud_suspendPermission);
        return;
    }
    slot->core.binding.suspension = maud_suspendNone;
    Publish(context, slot);
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyStreamResumed,
                                      .streamId = maudStreamIdOf(context, slot),
                                  });
}

static void Move(maudContext* context, maudStreamSlot* slot, maudDeviceId device)
{
    maudStreamCore* core = &slot->core;
    maudStreamId id = maudStreamIdOf(context, slot);
    core->binding.current = device;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyStreamMoved,
                                      .deviceId = device,
                                      .streamId = id,
                                  });
    // A backend whose streams are bound to one endpoint reopens them on
    // every move; the others only when the rate changed.
    if (TakeDeviceRate(context, slot) || context->backend->reopensOnMove)
    {
        Retarget(context, slot);
    }
    if (core->binding.suspension != maud_suspendNone &&
        core->binding.suspension != maud_suspendPolicy)
    {
        Resume(context, slot);
    }
}

void maudBindNewStream(maudContext* context, maudStreamSlot* slot)
{
    maudStreamCore* core = &slot->core;
    const maudStreamDef* def = &core->def;
    maudDeviceId device = def->device;
    if (device.index1 == 0)
    {
        device = context->devices.defaults[def->direction][def->role];
    }
    core->binding = (maudStreamBinding){
        .requested = def->device,
        .current = device,
        .started = false,
        .suspension = device.index1 == 0       ? maud_suspendNoDevice
                      : context->hostSuspended ? maud_suspendHost
                      : context->held          ? maud_suspendPolicy
                                               : maud_suspendNone,
    };
    atomic_store_explicit(&core->state, maud_streamIdle, memory_order_release);
}

void maudFollowDefault(maudContext* context, maudDirection direction, maudDeviceRole role)
{
    maudDeviceId target = context->devices.defaults[direction][role];
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        const maudStreamCore* core = &slot->core;
        if (!slot->live || core->binding.requested.index1 != 0 ||
            core->def.direction != direction || core->def.role != role ||
            SameDevice(core->binding.current, target))
        {
            continue;
        }
        if (target.index1 == 0)
        {
            Suspend(context, slot, maud_suspendNoDevice);
        }
        else
        {
            Move(context, slot, target);
        }
    }
}

void maudRefreshNativeRates(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live)
        {
            ApplyDeviceRate(context, slot);
        }
    }
}

void maudLoseDevice(maudContext* context, maudDeviceId device)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live && SameDevice(slot->core.binding.requested, device))
        {
            Suspend(context, slot, maud_suspendDeviceLost);
        }
    }
}

void maudSetStreamStarted(maudContext* context, maudStreamSlot* slot, bool started)
{
    slot->core.binding.started = started;
    Publish(context, slot);
}

void maudHoldStreams(maudContext* context, bool held)
{
    if (context->held == held)
    {
        return;
    }
    context->held = held;
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        maudSuspendReason reason = slot->core.binding.suspension;
        if (slot->live && held && reason == maud_suspendNone)
        {
            Wait(context, slot, maud_suspendPolicy);
        }
        else if (slot->live && !held && reason == maud_suspendPolicy)
        {
            Resume(context, slot);
        }
    }
}

// Whether a reason only keeps a stream from running, which a host
// suspension replaces, as opposed to a lost device, which it does not.
static bool OnlyWaiting(maudSuspendReason reason)
{
    return reason == maud_suspendNone || reason == maud_suspendPolicy ||
           reason == maud_suspendPermission;
}

void maudSuspendForHost(maudContext* context, bool suspended)
{
    if (context->hostSuspended == suspended)
    {
        return;
    }
    context->hostSuspended = suspended;
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        maudSuspendReason reason = slot->core.binding.suspension;
        if (slot->live && suspended && OnlyWaiting(reason))
        {
            Wait(context, slot, maud_suspendHost);
        }
        else if (slot->live && !suspended && reason == maud_suspendHost)
        {
            Resume(context, slot);
        }
    }
}

void maudAwaitPermission(maudContext* context, maudStreamSlot* slot, bool waiting)
{
    maudStreamBinding* binding = &slot->core.binding;
    binding->awaitingPermission = waiting;
    if (waiting && binding->suspension == maud_suspendNone)
    {
        Wait(context, slot, maud_suspendPermission);
    }
    else if (!waiting && binding->suspension == maud_suspendPermission)
    {
        Resume(context, slot);
    }
}
