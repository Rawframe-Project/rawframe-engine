// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// D3D12 objects for vendor upscalers (maul-rhi/d3d12.h, mrhi-0019): the
// functions a program integrating one calls, answered by the D3D12
// driver and unsupported on every other.

#include "device_core.h"
#include "encoder_core.h"
#include "validation.h"

#include "maul-rhi/d3d12.h"

#ifdef MAUL_RHI_D3D12_DRIVER
#include "d3d12_device.h"
#endif

mrhiResult mrhiGetD3d12Device(mrhiDevice* device, void** deviceOut, void** queueOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (deviceOut == nullptr || queueOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_D3D12_DRIVER
    ID3D12Device* native = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    if (!mrhiD3d12DeviceNative(mrhiInnerDevice(&device->driver), &native, &queue))
    {
        return mrhi_errorUnsupported;
    }
    *deviceOut = native;
    *queueOut = queue;
    return mrhi_success;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetD3d12Texture(mrhiDevice* device, mrhiTextureId texture, void** resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_D3D12_DRIVER
    if (device->adapterInfo.driver != mrhi_driverD3d12)
    {
        return mrhi_errorUnsupported;
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    ID3D12Resource* resource = nullptr;
    if (!mrhiD3d12DeviceTexture(mrhiInnerDevice(&device->driver),
                                device->textureSlots[texture.index1 - 1].handle, &resource))
    {
        return mrhi_errorUnsupported;
    }
    *resourceOut = resource;
    return mrhi_success;
#else
    (void)texture;
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiSetD3d12PassCommands(mrhiDevice* device, mrhiPassId pass, void* commandList)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->adapterInfo.driver != mrhi_driverD3d12)
    {
        return mrhi_errorUnsupported;
    }
    return mrhiSetNativeCommands(device, pass, commandList);
}
