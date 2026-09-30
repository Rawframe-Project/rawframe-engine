// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver (mrhi-0003): the system's DXGI adapters that open a
// D3D12 device at the floor, listed as adapters, requests answered at
// the next poll.

#ifndef MAUL_RHI_SRC_DRIVER_D3D12_H
#define MAUL_RHI_SRC_DRIVER_D3D12_H

#include "driver.h"

// Starts the D3D12 driver, holding at most pendingLimit unanswered
// requests: success with the driver, or with none on a system without
// d3d12.dll and dxgi.dll or a DXGI factory; mrhi_errorCapacity when the
// allocator fails.
mrhiResult mrhiCreateD3d12Driver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                 mrhiInstanceDriver* driverOut);

#endif // MAUL_RHI_SRC_DRIVER_D3D12_H
