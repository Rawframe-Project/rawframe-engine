// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Memory through a def's allocator (conventions section 9): a zeroed
// allocator means the C library's functions, which serve alignments up
// to that of max_align_t. A layout lays out a block's parts with checked
// arithmetic (conventions section 8), as Maul RHI's does.

#ifndef MAUL_WINDOW_SRC_ALLOCATOR_H
#define MAUL_WINDOW_SRC_ALLOCATOR_H

#include "maul-window/base.h"

#include <stdbool.h>

// Whether an allocator is usable: both functions set, or neither.
bool mwinIsAllocatorValid(const mwinAllocator* allocator);

// size bytes aligned to alignment, a power of two, or NULL.
void* mwinAllocate(const mwinAllocator* allocator, size_t size, size_t alignment);

// Returns memory mwinAllocate gave, with the same size and alignment.
void mwinRelease(const mwinAllocator* allocator, void* memory, size_t size, size_t alignment);

// A block being laid out: its size so far, and whether a part did not
// fit in size_t.
typedef struct mwinLayout
{
    size_t size;
    bool overflow;
} mwinLayout;

// Adds count items of itemSize bytes aligned to alignment, a power of
// two, and returns the part's offset (0 after an overflow).
size_t mwinLayoutAdd(mwinLayout* layout, size_t count, size_t itemSize, size_t alignment);

// Frees memory a system library allocated with the C library's malloc
// and hands over to its caller, as XCB does with its replies. NULL is
// nothing.
void mwinReleaseSystemMemory(void* memory);

#endif // MAUL_WINDOW_SRC_ALLOCATOR_H
