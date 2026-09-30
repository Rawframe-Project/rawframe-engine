// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver (mrhi-0003): the system's Metal devices listed as
// adapters, requests answered at the next poll. Its files are
// Objective-C, compiled on Apple systems only.

#ifndef MAUL_RHI_SRC_DRIVER_METAL_H
#define MAUL_RHI_SRC_DRIVER_METAL_H

#include "driver.h"

// Starts the Metal driver, holding at most pendingLimit unanswered
// requests: success with the driver, whose searches find no adapter on
// a system without Metal, or mrhi_errorCapacity when the allocator
// fails.
mrhiResult mrhiCreateMetalDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                 mrhiInstanceDriver* driverOut);

#endif // MAUL_RHI_SRC_DRIVER_METAL_H
