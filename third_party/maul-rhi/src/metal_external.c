// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Metal objects for vendor upscalers and MetalFX (maul-rhi/metal.h,
// mrhi-0019): the functions a program integrating one calls, answered by
// the Metal driver and unsupported on every other.

#include "device_core.h"
#include "encoder_core.h"
#include "validation.h"

#include "maul-rhi/metal.h"

#ifdef MAUL_RHI_METAL_DRIVER
#include "metal_native.h"
#endif

mrhiResult mrhiGetMetalDevice(mrhiDevice* device, void** deviceOut, void** queueOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (deviceOut == nullptr || queueOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_METAL_DRIVER
    return mrhiMetalDeviceNative(mrhiInnerDevice(&device->driver), deviceOut, queueOut)
               ? mrhi_success
               : mrhi_errorUnsupported;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetMetalTexture(mrhiDevice* device, mrhiTextureId texture, void** textureOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (textureOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_METAL_DRIVER
    if (device->adapterInfo.driver != mrhi_driverMetal)
    {
        return mrhi_errorUnsupported;
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    *textureOut = mrhiMetalTextureNative(device->textureSlots[texture.index1 - 1].handle);
    return mrhi_success;
#else
    (void)texture;
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiSetMetalPassCommands(mrhiDevice* device, mrhiPassId pass, void* commandBuffer)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->adapterInfo.driver != mrhi_driverMetal)
    {
        return mrhi_errorUnsupported;
    }
    return mrhiSetNativeCommands(device, pass, commandBuffer);
}
