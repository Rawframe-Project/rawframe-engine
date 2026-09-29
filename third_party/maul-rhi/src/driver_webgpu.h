// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU driver (mrhi-0003): the browser's WebGPU reached through
// EM_JS glue, its promises settled into a queue the driver's poll drains.

#ifndef MAUL_RHI_SRC_DRIVER_WEBGPU_H
#define MAUL_RHI_SRC_DRIVER_WEBGPU_H

#include "driver.h"

// Starts the WebGPU driver, holding at most pendingLimit unanswered
// requests: success with the driver, whose searches find no adapter in a
// browser without WebGPU, or mrhi_errorCapacity when the allocator fails.
mrhiResult mrhiCreateWebGpuDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                  mrhiInstanceDriver* driverOut);

#endif // MAUL_RHI_SRC_DRIVER_WEBGPU_H
