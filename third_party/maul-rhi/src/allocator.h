// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Memory through a def's allocator (conventions section 9): a zeroed
// allocator means the C library's functions, which serve alignments up
// to that of max_align_t.

#ifndef MAUL_RHI_SRC_ALLOCATOR_H
#define MAUL_RHI_SRC_ALLOCATOR_H

#include "maul-rhi/base.h"

#include <stdbool.h>

// Whether an allocator is usable: both functions set, or neither.
bool mrhiIsAllocatorValid(const mrhiAllocator* allocator);

// size bytes aligned to alignment, a power of two, or NULL.
void* mrhiAllocate(const mrhiAllocator* allocator, size_t size, size_t alignment);

// Returns memory mrhiAllocate gave, with the same size and alignment.
void mrhiRelease(const mrhiAllocator* allocator, void* memory, size_t size, size_t alignment);

// A block laid out in parts, as one allocation: each part's offset is
// aligned, and a size past SIZE_MAX sets overflow instead of wrapping.
typedef struct mrhiLayout
{
    size_t size;
    bool overflow;
} mrhiLayout;

// Adds count items of itemSize bytes aligned to alignment, a power of
// two, and returns the part's offset (0 after an overflow).
size_t mrhiLayoutAdd(mrhiLayout* layout, size_t count, size_t itemSize, size_t alignment);

#endif // MAUL_RHI_SRC_ALLOCATOR_H
