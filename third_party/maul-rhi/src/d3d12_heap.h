// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's heaps (mrhi-0015): each heap a region of resource
// descriptors and one of samplers at the front of every frame slot's
// shader-visible heaps, at its slot's place, entries written into every
// slot's region as the core accepts them. Included by the driver's
// files only.

#ifndef MAUL_RHI_SRC_D3D12_HEAP_H
#define MAUL_RHI_SRC_D3D12_HEAP_H

#include "d3d12_frame.h"

// Takes a heap's slot: success with its handle, or mrhi_errorCapacity.
mrhiResult mrhiD3d12CreateHeap(mrhiD3d12Frames* frames, uint64_t* handleOut);

// Writes a resource entry, or a sampler entry, the core has checked
// into every slot's region of the heap.
void mrhiD3d12WriteHeapEntry(const mrhiD3d12Frames* frames, uint64_t heap, uint32_t index,
                             const mrhiDriverHeapEntry* entry);
void mrhiD3d12WriteHeapSampler(const mrhiD3d12Frames* frames, uint64_t heap, uint32_t index,
                               uint64_t sampler);

#endif // MAUL_RHI_SRC_D3D12_HEAP_H
