// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland outputs as monitors.

#include "wayland_output.h"

#include <string.h>

// wl_output 4 added the name and description events.
#define OUTPUT_VERSION 4

static void OnGeometry(void* data, struct wl_output* output, int32_t x, int32_t y, int32_t widthMm,
                       int32_t heightMm, int32_t subpixel, const char* make, const char* model,
                       int32_t transform)
{
    (void)output;
    (void)subpixel;
    (void)make;
    (void)model;
    mwinWaylandOutput* found = data;
    found->x = x;
    found->y = y;
    found->info.widthMm = widthMm > 0 ? (uint32_t)widthMm : 0;
    found->info.heightMm = heightMm > 0 ? (uint32_t)heightMm : 0;
    found->transform = transform;
}

static void OnMode(void* data, struct wl_output* output, uint32_t flags, int32_t width,
                   int32_t height, int32_t refresh)
{
    (void)output;
    mwinWaylandOutput* found = data;
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0 || width <= 0 || height <= 0)
    {
        return;
    }
    found->info.bounds.width = (uint32_t)width;
    found->info.bounds.height = (uint32_t)height;
    found->info.refreshMilliHz = refresh > 0 ? (uint32_t)refresh : 0;
}

static void OnScale(void* data, struct wl_output* output, int32_t factor)
{
    (void)output;
    mwinWaylandOutput* found = data;
    found->scale = factor > 0 ? factor : 1;
}

static void OnName(void* data, struct wl_output* output, const char* name)
{
    (void)output;
    mwinWaylandOutput* found = data;
    size_t length = strnlen(name, MWIN_MONITOR_NAME_BYTES);
    memcpy(found->info.name, name, length);
    found->info.nameLength = (uint32_t)length;
}

static void OnDescription(void* data, struct wl_output* output, const char* description)
{
    (void)data;
    (void)output;
    (void)description;
}

// The facts of the last events hold together: the monitor appears or
// changes.
static void OnDone(void* data, struct wl_output* output)
{
    (void)output;
    mwinWaylandOutput* found = data;
    mwinContext* context = found->platform->context;
    mwinMonitorInfo info = found->info;
    info.bounds.x = found->x;
    info.bounds.y = found->y;
    // A quarter turn stands the mode on its side.
    if ((found->transform & 1) != 0)
    {
        info.bounds.width = found->info.bounds.height;
        info.bounds.height = found->info.bounds.width;
    }
    info.workArea = info.bounds;
    info.scale = (float)found->scale;
    if (found->monitor < 0)
    {
        found->monitor = mwinAddMonitor(context, &info, mwinMonotonicNow());
    }
    else
    {
        mwinChangeMonitor(context, (uint32_t)found->monitor, &info, mwinMonotonicNow());
    }
}

static const struct wl_output_listener s_outputListener = {
    OnGeometry, OnMode, OnDone, OnScale, OnName, OnDescription,
};

void mwinWaylandBindOutput(mwinWaylandPlatform* platform, uint32_t name, uint32_t version)
{
    const mwinWaylandApi* api = &platform->api;
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        mwinWaylandOutput* slot = &platform->outputs[i];
        if (slot->output != nullptr)
        {
            continue;
        }
        uint32_t bound = version < OUTPUT_VERSION ? version : OUTPUT_VERSION;
        *slot = (mwinWaylandOutput){.platform = platform, .name = name, .monitor = -1, .scale = 1};
        slot->output = (struct wl_output*)api->proxyMarshalFlags(
            (struct wl_proxy*)platform->registry, WL_REGISTRY_BIND, &wl_output_interface, bound, 0,
            name, wl_output_interface.name, bound, nullptr);
        mwinWlListen(api, slot->output, &s_outputListener, slot);
        return;
    }
}

static void Release(mwinWaylandPlatform* platform, mwinWaylandOutput* slot)
{
    const mwinWaylandApi* api = &platform->api;
    if (mwinWlVersion(api, slot->output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
    {
        (void)mwinWlRequest(api, slot->output, WL_OUTPUT_RELEASE, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
    else
    {
        api->proxyDestroy((struct wl_proxy*)slot->output);
    }
    slot->output = nullptr;
}

void mwinWaylandRemoveOutput(mwinWaylandPlatform* platform, uint32_t name)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        mwinWaylandOutput* slot = &platform->outputs[i];
        if (slot->output != nullptr && slot->name == name)
        {
            if (slot->monitor >= 0)
            {
                mwinRemoveMonitor(platform->context, (uint32_t)slot->monitor, mwinMonotonicNow());
            }
            Release(platform, slot);
            return;
        }
    }
}

void mwinWaylandReleaseOutputs(mwinWaylandPlatform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        if (platform->outputs[i].output != nullptr)
        {
            Release(platform, &platform->outputs[i]);
        }
    }
}

int32_t mwinWaylandMonitorOf(const mwinWaylandPlatform* platform, const struct wl_output* output)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        if (platform->outputs[i].output == output && output != nullptr)
        {
            return platform->outputs[i].monitor;
        }
    }
    return -1;
}
