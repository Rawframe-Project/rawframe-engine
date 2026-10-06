// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver (mrhi-0003): the loader opened at run time, physical
// devices listed as adapters, requests answered at the next poll.

#ifndef MAUL_RHI_SRC_DRIVER_VULKAN_H
#define MAUL_RHI_SRC_DRIVER_VULKAN_H

#include "driver.h"

// Starts the Vulkan driver, holding at most pendingLimit unanswered
// requests and reading at most adapterLimit physical devices, on an
// instance it makes (with the chain's extra extensions) or the chain's
// adopted one: success with the driver, success with no driver (a null
// vtable) where there is no loader or no Vulkan 1.3 instance, or
// mrhi_errorCapacity when the allocator fails.
mrhiResult mrhiCreateVulkanDriver(const mrhiAllocator* allocator, const mrhiChain* chain,
                                  uint32_t pendingLimit, uint32_t adapterLimit,
                                  mrhiInstanceDriver* driverOut);

// Whether an instance's driver is this one.
bool mrhiIsVulkanDriver(const mrhiInstanceDriver* driver);

// An adapter's VkPhysicalDevice.
void* mrhiVulkanPhysicalDevice(uint64_t adapter);

// Ends the last device description.
void mrhiForgetVulkanDevice(const mrhiInstanceDriver* driver);

// Describes the device a def makes on an adapter (mrhi-0018): its
// VkDeviceCreateInfo, which the driver holds until the next description
// or its end; mrhi_errorCapacity or mrhi_errorInvalid as the recipe
// fails.
mrhiResult mrhiDescribeVulkanDriverDevice(const mrhiInstanceDriver* driver, uint64_t adapter,
                                          const mrhiDeviceDef* def, void** infoOut);

#endif // MAUL_RHI_SRC_DRIVER_VULKAN_H
