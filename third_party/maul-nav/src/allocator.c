// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Allocation through the caller's allocator, counted against a byte
// limit; the one place a zeroed allocator reaches the C library.

#include "allocator.h"

#include "fpenv.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The host's allocator runs in the host's floating-point environment:
// each call is the only work of a helper of its own, so none of the
// caller's arithmetic moves into it.
static MNAV_NOINLINE void* HostAlloc(const mnavAllocator* allocator, size_t bytes, size_t alignment)
{
    mnavFloatHost host = mnavFloatHostEnter();
    void* block = allocator->alloc(bytes, alignment, allocator->context);
    mnavFloatHostLeave(host);
    return block;
}

static MNAV_NOINLINE void HostFree(const mnavAllocator* allocator, void* block, size_t bytes,
                                   size_t alignment)
{
    mnavFloatHost host = mnavFloatHostEnter();
    allocator->free(block, bytes, alignment, allocator->context);
    mnavFloatHostLeave(host);
}

mnavMemory mnavMakeMemory(mnavAllocator allocator, uint64_t limit)
{
    return (mnavMemory){allocator, limit, 0, 0};
}

mnavResult mnavAllocate(mnavMemory* memory, size_t count, size_t size, size_t alignment, void** out)
{
    *out = nullptr;
    size_t bytes = 0;
    uint64_t total = 0;
    if (ckd_mul(&bytes, count, size) || ckd_add(&total, memory->used, (uint64_t)bytes))
    {
        return mnav_errorCapacity;
    }
    if (total > memory->limit)
    {
        return mnav_errorLimit;
    }
    if (bytes == 0)
    {
        return mnav_success;
    }
    void* block = nullptr;
    if (memory->allocator.alloc != nullptr)
    {
        block = HostAlloc(&memory->allocator, bytes, alignment);
    }
    else if (alignment <= alignof(max_align_t))
    {
        block = malloc(bytes);
    }
    if (block == nullptr)
    {
        return mnav_errorCapacity;
    }
    memory->used = total;
    memory->peak = total > memory->peak ? total : memory->peak;
    *out = block;
    return mnav_success;
}

void mnavRelease(mnavMemory* memory, void* block, size_t count, size_t size, size_t alignment)
{
    if (block == nullptr)
    {
        return;
    }
    size_t bytes = count * size;
    memory->used -= bytes;
    if (memory->allocator.free != nullptr)
    {
        HostFree(&memory->allocator, block, bytes, alignment);
        return;
    }
    free(block);
}

mnavResult mnavReserve(mnavMemory* memory, void** block, int32_t* capacity, int32_t count,
                       int32_t needed, size_t size, size_t alignment)
{
    if (needed <= *capacity)
    {
        return mnav_success;
    }
    int64_t doubled = (int64_t)*capacity * 2;
    int64_t grown = doubled > needed ? doubled : needed;
    grown = grown < 16 ? 16 : grown;
    grown = grown > INT32_MAX ? INT32_MAX : grown;
    void* fresh = nullptr;
    mnavResult result = mnavAllocate(memory, (size_t)grown, size, alignment, &fresh);
    if (result != mnav_success)
    {
        return result;
    }
    if (count > 0 && fresh != nullptr && *block != nullptr)
    {
        memcpy(fresh, *block, (size_t)count * size);
    }
    mnavRelease(memory, *block, (size_t)*capacity, size, alignment);
    *block = fresh;
    *capacity = (int32_t)grown;
    return mnav_success;
}
