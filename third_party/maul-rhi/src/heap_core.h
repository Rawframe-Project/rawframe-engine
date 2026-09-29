// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bindless heaps inside the core (heap.c): what a pass, the device's end
// and the destruction of the objects heaps name need of them.

#ifndef MAUL_RHI_SRC_HEAP_CORE_H
#define MAUL_RHI_SRC_HEAP_CORE_H

#include "device_core.h"

#include "maul-rhi/heap.h"

// The kinds of object a heap entry names.
typedef enum mrhiHeapObject
{
    mrhiHeapObjectView,
    mrhiHeapObjectBuffer,
    mrhiHeapObjectSampler,
} mrhiHeapObject;

// Empties every entry of every heap that names an object being
// destroyed, by its slot.
void mrhiForgetHeapObject(mrhiDevice* device, mrhiHeapObject kind, uint32_t index1);

// Checks the heap a pass def names: success with its driver handle, 0
// for none, or mrhi_errorStale.
mrhiResult mrhiCheckPassHeap(mrhiDevice* device, mrhiHeapId heap, uint64_t* handleOut);

// Destroys every heap left, at the device's end.
void mrhiDestroyHeaps(mrhiDevice* device);

#endif // MAUL_RHI_SRC_HEAP_CORE_H
