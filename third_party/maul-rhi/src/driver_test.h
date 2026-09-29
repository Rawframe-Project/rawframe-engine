// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test driver (mrhi-0003): adapters a test describes, and requests
// answered at the next poll.

#ifndef MAUL_RHI_SRC_DRIVER_TEST_H
#define MAUL_RHI_SRC_DRIVER_TEST_H

#include "driver.h"

#include "maul-rhi/test.h"

// Makes a test driver from its def, holding at most pendingLimit
// unanswered requests: success, mrhi_errorInvalid for a def with
// adapters but no array, or mrhi_errorCapacity when the allocator fails.
mrhiResult mrhiCreateTestDriver(const mrhiAllocator* allocator, const mrhiTestDriverDef* def,
                                uint32_t pendingLimit, mrhiInstanceDriver* driverOut);

#endif // MAUL_RHI_SRC_DRIVER_TEST_H
