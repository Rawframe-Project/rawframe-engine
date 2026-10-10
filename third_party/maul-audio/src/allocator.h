// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Memory through a def's allocator (conventions section 9): a zeroed
// allocator means the C library's functions, which serve alignments up
// to that of max_align_t. A layout lays out a block's parts with checked
// arithmetic (conventions section 8).

#ifndef MAUL_AUDIO_SRC_ALLOCATOR_H
#define MAUL_AUDIO_SRC_ALLOCATOR_H

#include "maul-audio/base.h"

#include <stdbool.h>

// Whether an allocator is usable: both functions set, or neither.
bool maudIsAllocatorValid(const maudAllocator* allocator);

// size bytes aligned to alignment, a power of two, or NULL.
void* maudAllocate(const maudAllocator* allocator, size_t size, size_t alignment);

// Returns memory maudAllocate gave, with the same size and alignment.
void maudRelease(const maudAllocator* allocator, void* memory, size_t size, size_t alignment);

// Under AddressSanitizer a layout leaves a poisoned gap after each part,
// as LLVM's arena allocator does, an item long (16 bytes at least), so
// that a read past one array of a block is reported rather than landing
// in the next; a release build lays out the same block without them.
#if defined(__SANITIZE_ADDRESS__)
#define MAUD_REDZONES 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MAUD_REDZONES 1
#endif
#endif

// The gaps a layout records; parts past them get none.
#define MAUD_LAYOUT_GAPS 32

// A block being laid out: its size so far, whether a part did not fit
// in size_t, and under AddressSanitizer the gaps after its parts.
typedef struct maudLayout
{
    size_t size;
    bool overflow;
#if MAUD_REDZONES
    uint32_t gapCount;
    size_t gaps[MAUD_LAYOUT_GAPS];
    size_t spans[MAUD_LAYOUT_GAPS];
#endif
} maudLayout;

// Adds count items of itemSize bytes aligned to alignment, a power of
// two, and returns the part's offset (0 after an overflow).
size_t maudLayoutAdd(maudLayout* layout, size_t count, size_t itemSize, size_t alignment);

// Poisons the gaps of a laid-out block once it is allocated, under
// AddressSanitizer; does nothing otherwise. maudRelease unpoisons the
// whole block before it goes back to the allocator.
void maudLayoutPoison(const maudLayout* layout, void* block);

#endif // MAUL_AUDIO_SRC_ALLOCATOR_H
