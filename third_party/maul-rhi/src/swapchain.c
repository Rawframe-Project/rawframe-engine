// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surface configuration (mrhi-0007): a device configures a surface of its
// instance with a color, usages, a size and modes the surface reports on
// the device's adapter, and keeps the driver's swapchain in its table.
// A surface is configured on one device at a time.

#include "capabilities_core.h"
#include "device_core.h"
#include "instance_core.h"

#define SURFACE_CONFIG_COOKIE 0x6D727363u

mrhiSurfaceConfig mrhiDefaultSurfaceConfig(void)
{
    mrhiSurfaceConfig config = {0};
    config.cookie = SURFACE_CONFIG_COOKIE;
    config.usage = mrhi_textureRenderTarget;
    config.presentMode = mrhi_presentFifo;
    config.alphaMode = mrhi_alphaOpaque;
    return config;
}

// Whether a mode is one known bit.
static bool IsOneOf(uint32_t mode, uint32_t known)
{
    return mode != 0 && (mode & (mode - 1)) == 0 && (mode & ~known) == 0;
}

// Whether the config's own values are well formed.
static bool IsWellFormed(const mrhiSurfaceConfig* config)
{
    const mrhiSurfaceColor* color = &config->color;
    bool known = mrhiIsFormatKnown(color->format) && color->primaries <= mrhi_primariesDisplayP3 &&
                 color->transfer <= mrhi_transferPq && color->range <= mrhi_rangeExtended;
    mrhiFormat twin = mrhiFormatSrgbPair(color->format);
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        mrhiFormat format = config->viewFormats[i];
        if (format != mrhi_formatNone && format != twin)
        {
            return false;
        }
    }
    mrhiTextureUsage usage = config->usage;
    return known && usage != 0 && (usage & ~mrhiTextureUsageKnown) == 0 &&
           (usage & mrhi_textureTransient) == 0 && config->width > 0 && config->height > 0 &&
           IsOneOf(config->presentMode, mrhiPresentModesKnown) &&
           IsOneOf(config->alphaMode, mrhiAlphaModesKnown);
}

// Whether the surface reports the color.
static bool IsColorReported(const mrhiSurfaceCaps* caps, mrhiSurfaceColor color)
{
    for (uint32_t i = 0; i < caps->colorCount; ++i)
    {
        const mrhiSurfaceColor* reported = &caps->colors[i];
        if (reported->format == color.format && reported->primaries == color.primaries &&
            reported->transfer == color.transfer && reported->range == color.range)
        {
            return true;
        }
    }
    return false;
}

// Whether the surface takes the config's color: one it reports, or the
// sRGB twin of one's format where its images may be sRGB, and the twin
// as a view only where views may be.
static bool IsColorTaken(const mrhiSurfaceCaps* caps, const mrhiSurfaceConfig* config)
{
    bool views = false;
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        views = views || config->viewFormats[i] != mrhi_formatNone;
    }
    if (views && !caps->twinViews)
    {
        return false;
    }
    if (IsColorReported(caps, config->color))
    {
        return true;
    }
    mrhiSurfaceColor reported = config->color;
    reported.format = mrhiFormatSrgbPair(config->color.format);
    return caps->twinImages && reported.format != mrhi_formatNone &&
           IsColorReported(caps, reported);
}

// Whether the surface and the device can take the config.
static bool IsSupported(const mrhiDevice* device, const mrhiSurfaceCaps* caps,
                        const mrhiSurfaceConfig* config)
{
    uint32_t most = device->limits.textureDimension2d;
    return caps->presentable && IsColorTaken(caps, config) &&
           (config->usage & ~caps->usages) == 0 &&
           (config->presentMode & caps->presentModes) != 0 &&
           (config->alphaMode & caps->alphaModes) != 0 &&
           mrhiFormatTakes(device, config->color.format, config->usage) && config->width <= most &&
           config->height <= most;
}

// Checks a config's own values and names on a live device: success, or
// the refusal, with the surface's driver handle in handleOut.
static mrhiResult CheckNames(mrhiDevice* device, const mrhiSurfaceConfig* config,
                             uint64_t* handleOut)
{
    mrhiDefHead head = {config->cookie, config->next, nullptr, 0};
    mrhiResult status = mrhiCheckObjectDef(device, head, SURFACE_CONFIG_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!IsWellFormed(config))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticSurfaceConfig);
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    *handleOut = mrhiFindSurface(device->instance, config->surface);
    return *handleOut == 0 ? mrhi_errorStale : mrhi_success;
}

// Whether the open frame holds an image of a swapchain.
static bool HoldsImage(const mrhiDevice* device, uint32_t swapchain)
{
    const mrhiImport* acquired = &device->swapchainSlots[swapchain - 1].acquired;
    return device->frameOpen && acquired->frame == device->frameSerial && acquired->resource != 0;
}

// Checks a config on a live device and returns the surface's slot; NULL
// with the refusal in statusOut.
static mrhiSurfaceSlot* CheckConfig(mrhiDevice* device, const mrhiSurfaceConfig* config,
                                    mrhiResult* statusOut)
{
    uint64_t handle = 0;
    *statusOut = CheckNames(device, config, &handle);
    if (*statusOut != mrhi_success)
    {
        return nullptr;
    }
    mrhiInstance* instance = device->instance;
    mrhiSurfaceSlot* slot = &instance->surfaceSlots[config->surface.index1 - 1];
    if ((slot->device != nullptr && slot->device != device) ||
        (slot->device == device && HoldsImage(device, slot->swapchain)))
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    mrhiSurfaceCaps caps = {0};
    instance->driver.vtable->getSurfaceCaps(instance->driver.self, handle, device->adapter, &caps);
    if (!IsSupported(device, &caps, config))
    {
        *statusOut = mrhi_errorUnsupported;
        return nullptr;
    }
    return slot;
}

mrhiResult mrhiConfigureSurface(mrhiDevice* device, const mrhiSurfaceConfig* config)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (config == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiSurfaceSlot* surface = CheckConfig(device, config, &status);
    if (surface == nullptr)
    {
        return status;
    }
    uint32_t index1 = surface->swapchain;
    uint32_t generation = 0;
    if (surface->device == nullptr && !mrhiPoolAcquire(&device->swapchains, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    mrhiSwapchainSlot* slot = &device->swapchainSlots[index1 - 1];
    uint64_t old = surface->device == nullptr ? 0 : slot->handle;
    uint64_t handle = 0;
    status =
        mrhiDriverStatus(device, device->driver.vtable->configureSurface(
                                     device->driver.self, surface->handle, config, old, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->swapchains, index1);
        surface->device = nullptr;
        surface->swapchain = 0;
        return status;
    }
    *slot = (mrhiSwapchainSlot){.surface = config->surface, .handle = handle, .config = *config};
    slot->config.next = nullptr;
    surface->device = device;
    surface->swapchain = index1;
    return mrhi_success;
}

void mrhiEndConfiguration(mrhiDevice* device, uint32_t swapchain)
{
    const mrhiSwapchainSlot* slot = &device->swapchainSlots[swapchain - 1];
    device->driver.vtable->unconfigureSurface(device->driver.self, slot->handle);
    mrhiSurfaceSlot* surface = &device->instance->surfaceSlots[slot->surface.index1 - 1];
    surface->device = nullptr;
    surface->swapchain = 0;
    mrhiPoolRelease(&device->swapchains, swapchain);
}

void mrhiEndConfigurations(mrhiDevice* device)
{
    for (uint32_t index1 = 1; index1 <= device->swapchains.capacity; ++index1)
    {
        if (mrhiPoolIsLive(&device->swapchains, index1, device->swapchains.generations[index1 - 1]))
        {
            mrhiEndConfiguration(device, index1);
        }
    }
}

mrhiResult mrhiUnconfigureSurface(mrhiDevice* device, mrhiSurfaceId surface)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (mrhiFindSurface(device->instance, surface) == 0)
    {
        return mrhi_errorStale;
    }
    const mrhiSurfaceSlot* slot = &device->instance->surfaceSlots[surface.index1 - 1];
    if (slot->device != device || HoldsImage(device, slot->swapchain))
    {
        return mrhi_errorState;
    }
    mrhiEndConfiguration(device, slot->swapchain);
    return mrhi_success;
}
