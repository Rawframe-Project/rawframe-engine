// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The validation layer (mrhi-0025), in builds with MAUL_RHI_VALIDATION:
// an instance driver and its devices wrapped so that every frame the
// core submits is walked and every answer the driver gives is checked,
// each breach counted.

#ifndef MAUL_RHI_SRC_VALIDATION_H
#define MAUL_RHI_SRC_VALIDATION_H

#include "diagnostics.h"
#include "driver.h"

#include <stdatomic.h>

// Wraps a started driver in place, with room for tagLimit unanswered
// requests and breaches counted into faults and recorded in diagnostics:
// success, or
// mrhi_errorCapacity when the allocator fails, which leaves the driver
// as it was.
mrhiResult mrhiWrapDriver(const mrhiAllocator* allocator, uint32_t tagLimit,
                          _Atomic uint64_t* faults, mrhiDiagnosticQueue* diagnostics,
                          mrhiInstanceDriver* driver);

// The driver under the layer, for the native doors, which reach their
// own driver's objects; the driver itself in builds without the layer.
#ifdef MAUL_RHI_VALIDATION
const mrhiInstanceDriver* mrhiInnerDriver(const mrhiInstanceDriver* driver);
const mrhiDeviceDriver* mrhiInnerDevice(const mrhiDeviceDriver* device);
#else
static inline const mrhiInstanceDriver* mrhiInnerDriver(const mrhiInstanceDriver* driver)
{
    return driver;
}

static inline const mrhiDeviceDriver* mrhiInnerDevice(const mrhiDeviceDriver* device)
{
    return device;
}
#endif

#endif // MAUL_RHI_SRC_VALIDATION_H
