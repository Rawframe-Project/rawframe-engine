// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 monitors.

#include "x11_output.h"

#include "allocator.h"

#include <string.h>

// A monitor name from the atom that names it; empty where it cannot be
// read.
static void ReadName(const mwinX11Platform* platform, xcb_atom_t atom, mwinMonitorInfo* info)
{
    const mwinX11Api* api = &platform->api;
    xcb_get_atom_name_reply_t* reply = api->getAtomNameReply(
        platform->connection, api->getAtomName(platform->connection, atom), nullptr);
    if (reply == nullptr)
    {
        return;
    }
    int length = api->getAtomNameNameLength(reply);
    size_t size = length < MWIN_MONITOR_NAME_BYTES ? (size_t)length : MWIN_MONITOR_NAME_BYTES;
    memcpy(info->name, api->getAtomNameName(reply), size);
    info->nameLength = (uint32_t)size;
    mwinReleaseSystemMemory(reply);
}

// The output slot of a monitor name, taking a free one for a new name;
// -1 when every slot is taken.
static int32_t OutputOf(mwinX11Platform* platform, xcb_atom_t name)
{
    int32_t vacant = -1;
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        if (platform->outputs[i].name == name)
        {
            return (int32_t)i;
        }
        vacant = vacant < 0 && platform->outputs[i].name == XCB_ATOM_NONE ? (int32_t)i : vacant;
    }
    if (vacant >= 0)
    {
        platform->outputs[vacant] = (mwinX11Output){.name = name, .monitor = -1};
    }
    return vacant;
}

static bool SameRect(mwinPixelRect a, mwinPixelRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// Whether the facts the X server gives of a monitor are the same.
static bool SameFacts(const mwinMonitorInfo* a, const mwinMonitorInfo* b)
{
    return SameRect(a->bounds, b->bounds) && SameRect(a->workArea, b->workArea) &&
           a->widthMm == b->widthMm && a->heightMm == b->heightMm && a->scale == b->scale &&
           a->refreshMilliHz == b->refreshMilliHz && a->primary == b->primary;
}

// Adds a monitor or reports its change, when anything changed.
static void Report(mwinX11Platform* platform, mwinX11Output* output, mwinMonitorInfo* info)
{
    mwinContext* context = platform->context;
    output->seen = true;
    if (output->monitor < 0)
    {
        ReadName(platform, output->name, info);
        output->monitor = mwinAddMonitor(context, info, mwinMonotonicNow());
        return;
    }
    const mwinMonitorInfo* known = &context->monitors[output->monitor].info;
    memcpy(info->name, known->name, known->nameLength);
    info->nameLength = known->nameLength;
    if (!SameFacts(known, info))
    {
        mwinChangeMonitor(context, (uint32_t)output->monitor, info, mwinMonotonicNow());
    }
}

// The screen as the one monitor, without RandR 1.5.
static void ReportScreen(mwinX11Platform* platform)
{
    const xcb_screen_t* screen = platform->screen;
    int32_t slot = OutputOf(platform, XCB_ATOM_STRING);
    if (slot < 0)
    {
        return;
    }
    mwinMonitorInfo info = {0};
    info.bounds = (mwinPixelRect){0, 0, screen->width_in_pixels, screen->height_in_pixels};
    info.workArea = info.bounds;
    info.widthMm = screen->width_in_millimeters;
    info.heightMm = screen->height_in_millimeters;
    info.scale = platform->scale;
    info.primary = true;
    Report(platform, &platform->outputs[slot], &info);
}

static void ReportRandr(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    xcb_randr_get_monitors_reply_t* reply = api->randrGetMonitorsReply(
        platform->connection,
        api->randrGetMonitors(platform->connection, platform->screen->root, 1), nullptr);
    if (reply == nullptr)
    {
        return;
    }
    // Where RandR names no primary monitor, the first is.
    bool named = false;
    for (xcb_randr_monitor_info_iterator_t it = api->randrMonitorsIterator(reply); it.rem > 0;
         api->randrMonitorInfoNext(&it))
    {
        named |= it.data->primary != 0;
    }
    bool first = true;
    for (xcb_randr_monitor_info_iterator_t it = api->randrMonitorsIterator(reply); it.rem > 0;
         api->randrMonitorInfoNext(&it))
    {
        const xcb_randr_monitor_info_t* monitor = it.data;
        bool primary = monitor->primary != 0 || (!named && first);
        first = false;
        int32_t slot = OutputOf(platform, monitor->name);
        if (slot < 0)
        {
            continue;
        }
        mwinMonitorInfo info = {0};
        info.bounds = (mwinPixelRect){monitor->x, monitor->y, monitor->width, monitor->height};
        info.workArea = info.bounds;
        info.widthMm = monitor->width_in_millimeters;
        info.heightMm = monitor->height_in_millimeters;
        info.scale = platform->scale;
        info.primary = primary;
        Report(platform, &platform->outputs[slot], &info);
    }
    mwinReleaseSystemMemory(reply);
}

void mwinX11RefreshMonitors(mwinX11Platform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        platform->outputs[i].seen = false;
    }
    if (platform->randrEvent != 0)
    {
        ReportRandr(platform);
    }
    else
    {
        ReportScreen(platform);
    }
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        mwinX11Output* output = &platform->outputs[i];
        if (output->name != XCB_ATOM_NONE && !output->seen)
        {
            if (output->monitor >= 0)
            {
                mwinRemoveMonitor(platform->context, (uint32_t)output->monitor, mwinMonotonicNow());
            }
            *output = (mwinX11Output){.monitor = -1};
        }
    }
}

int32_t mwinX11MonitorAt(const mwinX11Platform* platform, int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        int32_t slot = platform->outputs[i].monitor;
        if (platform->outputs[i].name == XCB_ATOM_NONE || slot < 0)
        {
            continue;
        }
        const mwinPixelRect* bounds = &platform->context->monitors[slot].info.bounds;
        if (x >= bounds->x && y >= bounds->y && x < bounds->x + (int32_t)bounds->width &&
            y < bounds->y + (int32_t)bounds->height)
        {
            return slot;
        }
    }
    return -1;
}
