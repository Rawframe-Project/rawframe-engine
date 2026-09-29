// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Monitors: slots the backend fills as the platform reports hotplug,
// and the reads a program makes of them. A removed monitor's slot waits
// until its record is drained, so its id cannot come back while the
// program may still hold it.

#include "maul-window/monitor.h"

#include "core.h"

#include "maul-unicode/encoding.h"

mwinMonitorId mwinMonitorIdOf(const mwinContext* context, uint32_t slot)
{
    return (mwinMonitorId){slot + 1, context->monitors[slot].generation};
}

int32_t mwinFindMonitor(const mwinContext* context, mwinMonitorId monitor)
{
    if (monitor.index1 == 0 || monitor.index1 > context->limits.monitors)
    {
        return -1;
    }
    const mwinMonitor* found = &context->monitors[monitor.index1 - 1];
    return found->status == mwin_slotLive && found->generation == monitor.generation
               ? (int32_t)(monitor.index1 - 1)
               : -1;
}

// Keeps a monitor's facts, with a name that is not UTF-8 left empty.
static void Store(mwinMonitor* monitor, const mwinMonitorInfo* info)
{
    monitor->info = *info;
    if (info->nameLength > MWIN_MONITOR_NAME_BYTES ||
        muniValidateUtf8(info->name, info->nameLength).status != muni_success)
    {
        monitor->info.nameLength = 0;
    }
}

static void PostMonitor(mwinContext* context, uint32_t slot, mwinEventType type, uint64_t timeNs)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.monitor = mwinMonitorIdOf(context, slot);
    mwinPostGlobal(context, &event);
}

int32_t mwinAddMonitor(mwinContext* context, const mwinMonitorInfo* info, uint64_t timeNs)
{
    for (uint32_t i = 0; i < context->limits.monitors; i++)
    {
        mwinMonitor* monitor = &context->monitors[i];
        if (monitor->status == mwin_slotFree)
        {
            monitor->status = mwin_slotLive;
            monitor->generation += 1;
            Store(monitor, info);
            PostMonitor(context, i, mwin_eventMonitorAdded, timeNs);
            return (int32_t)i;
        }
    }
    return -1;
}

void mwinChangeMonitor(mwinContext* context, uint32_t slot, const mwinMonitorInfo* info,
                       uint64_t timeNs)
{
    Store(&context->monitors[slot], info);
    PostMonitor(context, slot, mwin_eventMonitorChanged, timeNs);
}

void mwinRemoveMonitor(mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    PostMonitor(context, slot, mwin_eventMonitorRemoved, timeNs);
    context->monitors[slot].status = mwin_slotDestroyed;
}

void mwinReleaseMonitor(mwinContext* context, mwinMonitorId monitor)
{
    mwinMonitor* found = &context->monitors[monitor.index1 - 1];
    if (found->status == mwin_slotDestroyed && found->generation == monitor.generation)
    {
        found->status = mwin_slotFree;
    }
}

mwinResult mwinGetMonitors(const mwinContext* context, mwinMonitorId* monitors, size_t capacity,
                           size_t* countOut)
{
    if (context == nullptr || countOut == nullptr || (monitors == nullptr && capacity != 0))
    {
        return mwin_errorInvalid;
    }
    size_t count = 0;
    // The primary first, then the others in slot order.
    for (int pass = 0; pass < 2; pass++)
    {
        for (uint32_t i = 0; i < context->limits.monitors; i++)
        {
            const mwinMonitor* monitor = &context->monitors[i];
            if (monitor->status != mwin_slotLive || monitor->info.primary != (pass == 0))
            {
                continue;
            }
            if (count < capacity)
            {
                monitors[count] = mwinMonitorIdOf(context, i);
            }
            count += 1;
        }
    }
    *countOut = count;
    return count > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinGetMonitorInfo(const mwinContext* context, mwinMonitorId monitor,
                              mwinMonitorInfo* infoOut)
{
    if (context == nullptr || infoOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    *infoOut = context->monitors[slot].info;
    return mwin_success;
}
