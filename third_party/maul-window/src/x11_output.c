// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 monitors.

#include "x11_output.h"

#include "allocator.h"
#include "edid.h"

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

// An output's property, of 8- or 32-bit items: NULL where it has none.
static xcb_randr_get_output_property_reply_t* PropertyOf(const mwinX11Platform* platform,
                                                         xcb_randr_output_t output, xcb_atom_t name,
                                                         uint32_t longs)
{
    const mwinX11Api* api = &platform->api;
    xcb_randr_get_output_property_reply_t* reply = api->randrGetOutputPropertyReply(
        platform->connection,
        api->randrGetOutputProperty(platform->connection, output, name, XCB_ATOM_ANY, 0, longs, 0,
                                    0),
        nullptr);
    if (reply != nullptr && reply->type == XCB_ATOM_NONE)
    {
        mwinReleaseSystemMemory(reply);
        return nullptr;
    }
    return reply;
}

// What an output says: HDR off, as X11 never shows it, with the
// luminances its EDID gives (mwin-0036); whether its driver can vary its
// refresh.
// A mode's refresh rate in millihertz, 0 where it tells no timing: an
// interlaced mode shows two fields a frame, a double-scanned one each
// line twice.
static uint32_t RefreshOf(const xcb_randr_mode_info_t* mode)
{
    uint64_t total = (uint64_t)mode->htotal * mode->vtotal;
    if ((mode->mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) != 0)
    {
        total *= 2;
    }
    if ((mode->mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) != 0)
    {
        total /= 2;
    }
    uint64_t rate = total != 0 ? ((uint64_t)mode->dot_clock * 1000 + total / 2) / total : 0;
    return rate <= UINT32_MAX ? (uint32_t)rate : 0;
}

// The refresh rate of an output's CRTC's mode, among the screen's modes.
static uint32_t OutputRefresh(const mwinX11Platform* platform, xcb_randr_output_t output,
                              const xcb_randr_get_screen_resources_current_reply_t* resources)
{
    const mwinX11Api* api = &platform->api;
    xcb_connection_t* connection = platform->connection;
    xcb_randr_get_output_info_reply_t* info = api->randrGetOutputInfoReply(
        connection, api->randrGetOutputInfo(connection, output, resources->config_timestamp),
        nullptr);
    xcb_randr_get_crtc_info_reply_t* crtc =
        info != nullptr && info->crtc != XCB_NONE
            ? api->randrGetCrtcInfoReply(
                  connection,
                  api->randrGetCrtcInfo(connection, info->crtc, resources->config_timestamp),
                  nullptr)
            : nullptr;
    uint32_t refresh = 0;
    const xcb_randr_mode_info_t* modes = api->randrResourceModes(resources);
    int count = api->randrResourceModesLength(resources);
    for (int i = 0; crtc != nullptr && i < count; i++)
    {
        refresh = modes[i].id == crtc->mode ? RefreshOf(&modes[i]) : refresh;
    }
    mwinReleaseSystemMemory(crtc);
    mwinReleaseSystemMemory(info);
    return refresh;
}

static void ReadOutputFacts(const mwinX11Platform* platform, xcb_randr_output_t output,
                            const xcb_randr_get_screen_resources_current_reply_t* resources,
                            mwinMonitorInfo* info)
{
    const mwinX11Api* api = &platform->api;
    info->refreshMilliHz = resources != nullptr ? OutputRefresh(platform, output, resources) : 0;
    // An EDID of the base block and up to 255 extensions.
    xcb_randr_get_output_property_reply_t* edid =
        PropertyOf(platform, output, platform->atoms[mwin_atomEdid], 256 * 128 / 4);
    mwinEdidHdr hdr = {0};
    if (edid != nullptr && edid->format == 8)
    {
        (void)mwinEdidHdrOf(api->randrOutputPropertyData(edid),
                            (size_t)api->randrOutputPropertyDataLength(edid), &hdr);
    }
    info->hdr = (mwinHdrFacts){
        .known = true,
        .peakNits = hdr.peakNits,
        .fullFrameNits = hdr.frameAverageNits,
        .headroom = 1.0f,
    };
    mwinReleaseSystemMemory(edid);
    xcb_randr_get_output_property_reply_t* vrr =
        PropertyOf(platform, output, platform->atoms[mwin_atomVrrCapable], 1);
    uint32_t capable = 0;
    if (vrr != nullptr && vrr->format == 32 && api->randrOutputPropertyDataLength(vrr) >= 4)
    {
        memcpy(&capable, api->randrOutputPropertyData(vrr), sizeof(capable));
    }
    info->variableRefresh = capable != 0;
    mwinReleaseSystemMemory(vrr);
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
    if (!mwinSameMonitorInfo(known, info))
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
    // The modes, whose timings give the refresh rates.
    xcb_randr_get_screen_resources_current_reply_t* resources = api->randrGetResourcesReply(
        platform->connection, api->randrGetResources(platform->connection, platform->screen->root),
        nullptr);
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
        // A monitor of several outputs (tiled) takes the first's facts.
        if (api->randrMonitorOutputsLength(monitor) > 0)
        {
            ReadOutputFacts(platform, api->randrMonitorOutputs(monitor)[0], resources, &info);
        }
        Report(platform, &platform->outputs[slot], &info);
    }
    mwinReleaseSystemMemory(resources);
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
