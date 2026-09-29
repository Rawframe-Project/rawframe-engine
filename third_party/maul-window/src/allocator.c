// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one place a zeroed allocator, or memory a system library handed
// over, reaches the C library.

#include "allocator.h"

#include <stddef.h>
#include <stdlib.h>

bool mwinIsAllocatorValid(const mwinAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

void* mwinAllocate(const mwinAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return allocator->alloc(size, alignment, allocator->context);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

void mwinRelease(const mwinAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
    if (allocator->free != nullptr)
    {
        allocator->free(memory, size, alignment, allocator->context);
        return;
    }
    free(memory);
}

void mwinReleaseSystemMemory(void* memory)
{
    free(memory);
}
