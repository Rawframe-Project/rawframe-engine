// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver (mrhi-0003): the loader opened at run time, physical
// devices listed as adapters, requests answered at the next poll.

#ifndef MAUL_RHI_SRC_DRIVER_VULKAN_H
#define MAUL_RHI_SRC_DRIVER_VULKAN_H

#include "driver.h"

// Starts the Vulkan driver, holding at most pendingLimit unanswered
// requests and reading at most adapterLimit physical devices: success
// with the driver, success with no driver (a null vtable) where there
// is no loader or no Vulkan 1.3 instance, or mrhi_errorCapacity when
// the allocator fails.
mrhiResult mrhiCreateVulkanDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                  uint32_t adapterLimit, mrhiInstanceDriver* driverOut);

#endif // MAUL_RHI_SRC_DRIVER_VULKAN_H
