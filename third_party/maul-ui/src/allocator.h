// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Memory through a def's allocator (conventions section 9): a zeroed
// allocator means the C library's functions, which serve alignments up
// to that of max_align_t. A layout places a block's parts with checked
// arithmetic (conventions section 8).

#ifndef MAUL_UI_SRC_ALLOCATOR_H
#define MAUL_UI_SRC_ALLOCATOR_H

#include "maul-ui/base.h"

#include <stdbool.h>

// Whether an allocator is usable: both functions set, or neither.
bool muiIsAllocatorValid(const muiAllocator* allocator);

// size bytes aligned to alignment, a power of two, or NULL.
void* muiAllocate(const muiAllocator* allocator, size_t size, size_t alignment);

// Returns memory muiAllocate gave, with the same size and alignment.
void muiRelease(const muiAllocator* allocator, void* memory, size_t size, size_t alignment);

// A block being laid out: its size so far, and whether a part did not
// fit in size_t.
typedef struct muiLayout
{
    size_t size;
    bool overflow;
} muiLayout;

// Adds count items of itemSize bytes aligned to alignment, a power of
// two, and returns the part's offset (0 after an overflow).
size_t muiLayoutAdd(muiLayout* layout, size_t count, size_t itemSize, size_t alignment);

#endif // MAUL_UI_SRC_ALLOCATOR_H
