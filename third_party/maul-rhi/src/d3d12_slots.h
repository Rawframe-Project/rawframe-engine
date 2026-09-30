// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's tables (mrhi-0003): free slots of a table in the
// device's block, linked by index plus one, a handle being its slot's
// index plus one.

#ifndef MAUL_RHI_SRC_D3D12_SLOTS_H
#define MAUL_RHI_SRC_D3D12_SLOTS_H

#include <stdint.h>

typedef struct mrhiD3d12Slots
{
    uint32_t* next;
    uint32_t head;
    uint32_t capacity;
} mrhiD3d12Slots;

// Sets up a table of capacity slots, all free, over its links, and
// answers where the links after them start.
uint32_t* mrhiD3d12InitSlots(mrhiD3d12Slots* slots, uint32_t* next, uint32_t capacity);

// A free slot's handle, taken: 0 when the table is full.
uint32_t mrhiD3d12TakeSlot(mrhiD3d12Slots* slots);
void mrhiD3d12GiveSlot(mrhiD3d12Slots* slots, uint64_t handle);

#endif // MAUL_RHI_SRC_D3D12_SLOTS_H
