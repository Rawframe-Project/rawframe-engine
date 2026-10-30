// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one place a zeroed allocator reaches the C library, and the
// checked layout of blocks.

#include "allocator.h"

#include "fpenv.h"
#include "invariant.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdlib.h>

bool muiIsAllocatorValid(const muiAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

// The host's allocator functions, run in the host's floating-point
// environment.
static MUI_NOINLINE void* HostAlloc(const muiAllocator* allocator, size_t size, size_t alignment)
{
    muiFloatHost host = muiFloatHostEnter();
    void* memory = allocator->alloc(size, alignment, allocator->context);
    muiFloatHostLeave(host);
    return memory;
}

static MUI_NOINLINE void HostFree(const muiAllocator* allocator, void* memory, size_t size,
                                  size_t alignment)
{
    muiFloatHost host = muiFloatHostEnter();
    allocator->free(memory, size, alignment, allocator->context);
    muiFloatHostLeave(host);
}

void* muiAllocate(const muiAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return HostAlloc(allocator, size, alignment);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

void muiRelease(const muiAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
    if (allocator->free != nullptr)
    {
        HostFree(allocator, memory, size, alignment);
        return;
    }
    free(memory);
}

size_t muiLayoutAdd(muiLayout* layout, size_t count, size_t itemSize, size_t alignment)
{
    size_t padded = 0;
    size_t bytes = 0;
    size_t end = 0;
    if (layout->overflow || ckd_add(&padded, layout->size, alignment - 1) ||
        ckd_mul(&bytes, count, itemSize))
    {
        layout->overflow = true;
        return 0;
    }
    size_t offset = padded & ~(alignment - 1);
    // A part is padded only up to its own alignment, never a whole one more.
    MUI_ASSERT((alignment & (alignment - 1)) == 0 && offset - layout->size < alignment);
    if (ckd_add(&end, offset, bytes))
    {
        layout->overflow = true;
        return 0;
    }
    layout->size = end;
    return offset;
}
