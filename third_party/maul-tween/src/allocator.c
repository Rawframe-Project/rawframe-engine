// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one place a zeroed allocator reaches the C library.
// The library asks it for max_align_t's alignment at most,
// which malloc serves. A host's allocator is host code, which runs in
// the host's own floating-point environment (src/fpenv.h).

#include "allocator.h"

#include "fpenv.h"

#include <stdlib.h>

static MTWN_NOINLINE void* HostAlloc(const mtwnAllocator* allocator, size_t bytes, size_t alignment)
{
    mtwnFloatHost host = mtwnFloatHostEnter();
    void* memory = allocator->alloc(bytes, alignment, allocator->context);
    mtwnFloatHostLeave(host);
    return memory;
}

static MTWN_NOINLINE void HostFree(const mtwnAllocator* allocator, void* memory, size_t bytes,
                                   size_t alignment)
{
    mtwnFloatHost host = mtwnFloatHostEnter();
    allocator->free(memory, bytes, alignment, allocator->context);
    mtwnFloatHostLeave(host);
}

void* mtwnAllocate(const mtwnAllocator* allocator, size_t bytes, size_t alignment)
{
    if (allocator->alloc != NULL)
    {
        return HostAlloc(allocator, bytes, alignment);
    }
    return malloc(bytes);
}

void mtwnRelease(const mtwnAllocator* allocator, void* memory, size_t bytes, size_t alignment)
{
    if (memory == NULL)
    {
        return;
    }
    if (allocator->free != NULL)
    {
        HostFree(allocator, memory, bytes, alignment);
        return;
    }
    free(memory);
}
