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

#include <string.h>

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

// Keeps a monitor's facts, its name cut to its longest well-formed
// prefix: a backend cuts a long name at MWIN_MONITOR_NAME_BYTES, which
// may split a character, and another program's bytes may be ill-formed.
static void Store(mwinMonitor* monitor, const mwinMonitorInfo* info)
{
    monitor->info = *info;
    uint32_t length =
        info->nameLength < MWIN_MONITOR_NAME_BYTES ? info->nameLength : MWIN_MONITOR_NAME_BYTES;
    muniTextResult valid = muniValidateUtf8(info->name, length);
    monitor->info.nameLength = valid.status == muni_success ? length : (uint32_t)valid.offset;
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

static bool SameRect(mwinPixelRect a, mwinPixelRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static bool SameHdr(mwinHdrFacts a, mwinHdrFacts b)
{
    return a.known == b.known && a.active == b.active && a.peakNits == b.peakNits &&
           a.fullFrameNits == b.fullFrameNits && a.sdrWhiteNits == b.sdrWhiteNits &&
           a.headroom == b.headroom;
}

bool mwinSameMonitorInfo(const mwinMonitorInfo* a, const mwinMonitorInfo* b)
{
    return a->nameLength == b->nameLength && memcmp(a->name, b->name, a->nameLength) == 0 &&
           SameRect(a->bounds, b->bounds) && SameRect(a->workArea, b->workArea) &&
           a->widthMm == b->widthMm && a->heightMm == b->heightMm && a->scale == b->scale &&
           a->refreshMilliHz == b->refreshMilliHz && a->variableRefresh == b->variableRefresh &&
           a->primary == b->primary && SameHdr(a->hdr, b->hdr);
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
        return mwinMisuse(context);
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
        return mwinMisuse(context);
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    *infoOut = context->monitors[slot].info;
    return mwin_success;
}
