// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's native objects for vendor upscalers (mrhi-0019),
// in C types, for the C file that answers maul-rhi/metal.h.

#ifndef MAUL_RHI_SRC_METAL_NATIVE_H
#define MAUL_RHI_SRC_METAL_NATIVE_H

#include "driver.h"

// Reads a device's id<MTLDevice> and id<MTLCommandQueue>, borrowed:
// false for a device driver that is not Metal's.
bool mrhiMetalDeviceNative(const mrhiDeviceDriver* driver, void** deviceOut, void** queueOut);

// Reads a device texture's id<MTLTexture>, borrowed.
void* mrhiMetalTextureNative(uint64_t handle);

#endif // MAUL_RHI_SRC_METAL_NATIVE_H
