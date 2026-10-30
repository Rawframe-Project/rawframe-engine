// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Allocation through the host's allocator, the C library's when it is
// zeroed.

#ifndef MAUL_TWEEN_SRC_ALLOCATOR_H
#define MAUL_TWEEN_SRC_ALLOCATOR_H

#include "maul-tween/base.h"

#include <stddef.h>

// Allocates bytes aligned to alignment, a power of two and, for a zeroed
// allocator, at most max_align_t's; NULL when the allocator fails.
void* mtwnAllocate(const mtwnAllocator* allocator, size_t bytes, size_t alignment);

// Frees what mtwnAllocate returned for the same size and alignment. NULL
// is ignored.
void mtwnRelease(const mtwnAllocator* allocator, void* memory, size_t bytes, size_t alignment);

#endif
