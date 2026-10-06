// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's devices (mrhi-0003). Included by the driver's
// files only.

#ifndef MAUL_RHI_SRC_D3D12_DEVICE_H
#define MAUL_RHI_SRC_D3D12_DEVICE_H

#include "d3d12_api.h"
#include "driver.h"

// The frames a device lets run at once.
#define MRHI_D3D12_FRAMES 3

// Makes a device driver around a D3D12 device, which it takes, with the
// driver's entry points and the factory its swapchains come from, which
// it holds: success, mrhi_errorUnsupported when its heaps leave the
// frames' descriptor rings less than their floor, mrhi_errorCapacity
// when the allocator fails, or mrhi_errorPlatform when D3D12 makes no
// queue. The device is released
// on failure.
mrhiResult mrhiCreateD3d12Device(const mrhiAllocator* allocator, const mrhiD3d12Api* api,
                                 IDXGIFactory4* factory, ID3D12Device* device,
                                 const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut);

// Reads a device's ID3D12Device and command queue, borrowed
// (mrhi-0019): false for a device driver that is not D3D12's.
bool mrhiD3d12DeviceNative(const mrhiDeviceDriver* driver, ID3D12Device** deviceOut,
                           ID3D12CommandQueue** queueOut);

// Reads a device texture's resource, borrowed: false for a device
// driver that is not D3D12's.
bool mrhiD3d12DeviceTexture(const mrhiDeviceDriver* driver, uint64_t handle,
                            ID3D12Resource** resourceOut);

#endif // MAUL_RHI_SRC_D3D12_DEVICE_H
