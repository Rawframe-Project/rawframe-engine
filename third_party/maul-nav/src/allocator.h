// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Allocation through the caller's allocator, counted against a byte
// limit.

#ifndef MAUL_NAV_SRC_ALLOCATOR_H
#define MAUL_NAV_SRC_ALLOCATOR_H

#include "host_code.h"

#include "maul-nav/base.h"

#include <stddef.h>
#include <stdint.h>

typedef struct mnavMemory
{
    mnavAllocator allocator;
    // The most bytes held at once; reaching past it is mnav_errorCapacity.
    uint64_t limit;
    // The bytes held now, and the most held so far.
    uint64_t used;
    uint64_t peak;
    // The object whose allocator this is, marked while the allocator
    // runs; NULL for none.
    mnavHostCode* host;
} mnavMemory;

// A memory counter over an allocator, marking no object; a zeroed
// allocator means the C library's functions.
mnavMemory mnavMakeMemory(mnavAllocator allocator, uint64_t limit);

// Allocates count elements of size bytes each, aligned to alignment, a
// power of two. Returns mnav_errorCapacity past the limit, when the size
// overflows or when the allocator fails.
mnavResult mnavAllocate(mnavMemory* memory, size_t count, size_t size, size_t alignment,
                        void** out);

// Frees what mnavAllocate returned for the same count, size and alignment.
// NULL is ignored.
void mnavRelease(mnavMemory* memory, void* block, size_t count, size_t size, size_t alignment);

// Grows an array of elements of size bytes holding count elements in a
// block of *capacity, so that it holds at least needed: a new block of
// twice the size (or needed, if larger), the elements copied over and the
// old block released. Does nothing when the block is large enough.
mnavResult mnavReserve(mnavMemory* memory, void** block, int32_t* capacity, int32_t count,
                       int32_t needed, size_t size, size_t alignment);

#endif // MAUL_NAV_SRC_ALLOCATOR_H
