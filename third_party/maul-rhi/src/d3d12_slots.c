// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's tables (d3d12_slots.h).

#include "d3d12_slots.h"

#include "invariant.h"

uint32_t* mrhiD3d12InitSlots(mrhiD3d12Slots* slots, uint32_t* next, uint32_t capacity)
{
    for (uint32_t i = 0; i < capacity; ++i)
    {
        next[i] = i + 1 < capacity ? i + 2 : 0;
    }
    *slots = (mrhiD3d12Slots){.next = next, .head = capacity > 0 ? 1 : 0, .capacity = capacity};
    return next + capacity;
}

uint32_t mrhiD3d12TakeSlot(mrhiD3d12Slots* slots)
{
    uint32_t handle = slots->head;
    if (handle != 0)
    {
        slots->head = slots->next[handle - 1];
    }
    return handle;
}

void mrhiD3d12GiveSlot(mrhiD3d12Slots* slots, uint64_t handle)
{
    MRHI_ASSERT(handle != 0 && handle <= slots->capacity);
    slots->next[handle - 1] = slots->head;
    slots->head = (uint32_t)handle;
}
