// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland outputs as monitors, and their image descriptions.

#include "wayland_output.h"

#include <string.h>
#include <unistd.h>

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

// The output's facts as they stand: its monitor appears, or changes when
// a fact did.
static void Publish(mwinWaylandOutput* found)
{
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
    else if (!mwinSameMonitorInfo(&context->monitors[found->monitor].info, &info))
    {
        mwinChangeMonitor(context, (uint32_t)found->monitor, &info, mwinMonotonicNow());
    }
}

// The facts of the last events hold together.
static void OnDone(void* data, struct wl_output* output)
{
    (void)output;
    mwinWaylandOutput* found = data;
    found->done = true;
    Publish(found);
}

static const struct wl_output_listener s_outputListener = {
    OnGeometry, OnMode, OnDone, OnScale, OnName, OnDescription,
};

// Lets go of the description asked for and its information, if any.
static void DropDescription(mwinWaylandOutput* found)
{
    const mwinWaylandApi* api = &found->platform->api;
    if (found->information != nullptr)
    {
        // It has no request: its done event ends it, or the client.
        api->proxyDestroy((struct wl_proxy*)found->information);
        found->information = nullptr;
    }
    if (found->description != nullptr)
    {
        (void)mwinWlRequest(api, found->description, WP_IMAGE_DESCRIPTION_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        found->description = nullptr;
    }
}

// New HDR facts: told once the output's own facts were.
static void SetHdr(mwinWaylandOutput* found, mwinHdrFacts hdr)
{
    found->info.hdr = hdr;
    if (found->done)
    {
        Publish(found);
    }
}

// The HDR facts an image description's information told. HDR output is
// on with a transfer function beyond SDR white; the peak is the content
// light level the output targets, or its target luminance; the headroom
// the peak over SDR white.
static mwinHdrFacts HdrOf(const mwinWaylandColorFacts* facts)
{
    uint32_t transfer = facts->transfer;
    float peak = facts->maxCll > 0.0f ? facts->maxCll : facts->targetMaxNits;
    mwinHdrFacts hdr = {
        .known = true,
        .active = transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
                  transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG ||
                  transfer == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR,
        .peakNits = peak,
        .fullFrameNits = facts->maxFall,
        .sdrWhiteNits = facts->referenceNits,
    };
    if (peak > 0.0f && facts->referenceNits > 0.0f)
    {
        float headroom = peak / facts->referenceNits;
        hdr.headroom = headroom > 1.0f ? headroom : 1.0f;
    }
    else
    {
        hdr.headroom = hdr.active ? 0.0f : 1.0f;
    }
    return hdr;
}

static void OnInfoDone(void* data, struct wp_image_description_info_v1* information)
{
    (void)information;
    mwinWaylandOutput* found = data;
    DropDescription(found);
    SetHdr(found, HdrOf(&found->colorFacts));
}

static void OnIccFile(void* data, struct wp_image_description_info_v1* information, int32_t icc,
                      uint32_t size)
{
    (void)data;
    (void)information;
    (void)size;
    (void)close(icc);
}

static void OnPrimaries(void* data, struct wp_image_description_info_v1* information, int32_t rx,
                        int32_t ry, int32_t gx, int32_t gy, int32_t bx, int32_t by, int32_t wx,
                        int32_t wy)
{
    (void)data;
    (void)information;
    (void)rx;
    (void)ry;
    (void)gx;
    (void)gy;
    (void)bx;
    (void)by;
    (void)wx;
    (void)wy;
}

static void OnPrimariesNamed(void* data, struct wp_image_description_info_v1* information,
                             uint32_t primaries)
{
    (void)data;
    (void)information;
    (void)primaries;
}

static void OnTfPower(void* data, struct wp_image_description_info_v1* information, uint32_t eexp)
{
    (void)data;
    (void)information;
    (void)eexp;
}

static void OnTfNamed(void* data, struct wp_image_description_info_v1* information, uint32_t tf)
{
    (void)information;
    mwinWaylandOutput* found = data;
    found->colorFacts.transfer = tf;
}

static void OnLuminances(void* data, struct wp_image_description_info_v1* information,
                         uint32_t minimum, uint32_t maximum, uint32_t reference)
{
    (void)information;
    (void)minimum;
    (void)maximum;
    mwinWaylandOutput* found = data;
    found->colorFacts.referenceNits = (float)reference;
}

static void OnTargetPrimaries(void* data, struct wp_image_description_info_v1* information,
                              int32_t rx, int32_t ry, int32_t gx, int32_t gy, int32_t bx,
                              int32_t by, int32_t wx, int32_t wy)
{
    OnPrimaries(data, information, rx, ry, gx, gy, bx, by, wx, wy);
}

static void OnTargetLuminance(void* data, struct wp_image_description_info_v1* information,
                              uint32_t minimum, uint32_t maximum)
{
    (void)information;
    (void)minimum;
    mwinWaylandOutput* found = data;
    found->colorFacts.targetMaxNits = (float)maximum;
}

static void OnTargetMaxCll(void* data, struct wp_image_description_info_v1* information,
                           uint32_t maxCll)
{
    (void)information;
    mwinWaylandOutput* found = data;
    found->colorFacts.maxCll = (float)maxCll;
}

static void OnTargetMaxFall(void* data, struct wp_image_description_info_v1* information,
                            uint32_t maxFall)
{
    (void)information;
    mwinWaylandOutput* found = data;
    found->colorFacts.maxFall = (float)maxFall;
}

static const struct wp_image_description_info_v1_listener s_infoListener = {
    OnInfoDone,        OnIccFile,      OnPrimaries,     OnPrimariesNamed,
    OnTfPower,         OnTfNamed,      OnLuminances,    OnTargetPrimaries,
    OnTargetLuminance, OnTargetMaxCll, OnTargetMaxFall,
};

// The compositor could not describe the output: its HDR facts are
// unknown.
static void OnFailed(void* data, struct wp_image_description_v1* description, uint32_t cause,
                     const char* message)
{
    (void)description;
    (void)cause;
    (void)message;
    mwinWaylandOutput* found = data;
    DropDescription(found);
    SetHdr(found, (mwinHdrFacts){0});
}

static void OnReady(void* data, struct wp_image_description_v1* description, uint32_t identity)
{
    (void)identity;
    mwinWaylandOutput* found = data;
    const mwinWaylandApi* api = &found->platform->api;
    found->colorFacts = (mwinWaylandColorFacts){0};
    found->information = mwinWlRequest(api, description, WP_IMAGE_DESCRIPTION_V1_GET_INFORMATION,
                                       &wp_image_description_info_v1_interface, 0);
    mwinWlListen(api, found->information, &s_infoListener, found);
}

static const struct wp_image_description_v1_listener s_descriptionListener = {
    OnFailed,
    OnReady,
};

// Asks for the output's image description now, letting go of one asked
// for before.
static void AskDescription(mwinWaylandOutput* found)
{
    const mwinWaylandApi* api = &found->platform->api;
    DropDescription(found);
    found->description =
        mwinWlRequest(api, found->color, WP_COLOR_MANAGEMENT_OUTPUT_V1_GET_IMAGE_DESCRIPTION,
                      &wp_image_description_v1_interface, 0);
    mwinWlListen(api, found->description, &s_descriptionListener, found);
}

static void OnDescriptionChanged(void* data, struct wp_color_management_output_v1* color)
{
    (void)color;
    AskDescription(data);
}

static const struct wp_color_management_output_v1_listener s_colorListener = {
    OnDescriptionChanged,
};

static void WatchColor(mwinWaylandOutput* found)
{
    mwinWaylandPlatform* platform = found->platform;
    const mwinWaylandApi* api = &platform->api;
    if (platform->colorManager == nullptr || found->color != nullptr)
    {
        return;
    }
    found->color = mwinWlCreateFor(api, platform->colorManager, WP_COLOR_MANAGER_V1_GET_OUTPUT,
                                   &wp_color_management_output_v1_interface, found->output);
    mwinWlListen(api, found->color, &s_colorListener, found);
    AskDescription(found);
}

void mwinWaylandWatchColors(mwinWaylandPlatform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        if (platform->outputs[i].output != nullptr)
        {
            WatchColor(&platform->outputs[i]);
        }
    }
}

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
        WatchColor(slot);
        return;
    }
}

static void Release(mwinWaylandPlatform* platform, mwinWaylandOutput* slot)
{
    const mwinWaylandApi* api = &platform->api;
    DropDescription(slot);
    if (slot->color != nullptr)
    {
        (void)mwinWlRequest(api, slot->color, WP_COLOR_MANAGEMENT_OUTPUT_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        slot->color = nullptr;
    }
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
