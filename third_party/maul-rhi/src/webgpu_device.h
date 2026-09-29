// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A WebGPU device (mrhi-0003): a GPUDevice asked of a fresh adapter,
// since an adapter makes one device, with the granted features and
// limits; its objects kept on the JavaScript side under small handles.

#ifndef MAUL_RHI_SRC_WEBGPU_DEVICE_H
#define MAUL_RHI_SRC_WEBGPU_DEVICE_H

#include "driver.h"

// Makes a device driver and starts opening its GPUDevice, whose outcome
// settles the instance state's request slot: mrhi_success, or
// mrhi_errorCapacity when the allocator fails.
mrhiResult mrhiCreateWebGpuDevice(const mrhiAllocator* allocator, int instanceState, uint32_t slot,
                                  const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut);

#endif // MAUL_RHI_SRC_WEBGPU_DEVICE_H
