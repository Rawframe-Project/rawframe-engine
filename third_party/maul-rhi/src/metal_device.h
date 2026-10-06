// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's devices (mrhi-0003): a Metal device with its
// command queue. Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_DEVICE_H
#define MAUL_RHI_SRC_METAL_DEVICE_H

#include "driver.h"

#import <Metal/Metal.h>

// Opens a device on a Metal device, which it retains, with its command
// queue labelled as the def says: success, mrhi_errorCapacity when the
// allocator fails, or mrhi_errorPlatform when Metal makes no queue.
mrhiResult mrhiCreateMetalDevice(const mrhiAllocator* allocator, id<MTLDevice> device,
                                 const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut);

#include "metal_native.h"

#endif // MAUL_RHI_SRC_METAL_DEVICE_H
