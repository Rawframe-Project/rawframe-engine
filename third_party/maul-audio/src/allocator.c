// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The one place a zeroed allocator reaches the C library, and the
// checked layout of blocks.

#include "allocator.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdlib.h>

#if MAUD_REDZONES
#include <sanitizer/asan_interface.h>

// The least gap: two of ASan's 8-byte granules.
#define REDZONE_BYTES 16u
#endif

bool maudIsAllocatorValid(const maudAllocator* allocator)
{
    return (allocator->alloc == nullptr) == (allocator->free == nullptr);
}

void* maudAllocate(const maudAllocator* allocator, size_t size, size_t alignment)
{
    if (allocator->alloc != nullptr)
    {
        return allocator->alloc(size, alignment, allocator->context);
    }
    return alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
}

void maudRelease(const maudAllocator* allocator, void* memory, size_t size, size_t alignment)
{
    if (memory == nullptr)
    {
        return;
    }
#if MAUD_REDZONES
    // A host's allocator may hand the block out again: no gap stays
    // poisoned past its owner.
    ASAN_UNPOISON_MEMORY_REGION(memory, size);
#endif
    if (allocator->free != nullptr)
    {
        allocator->free(memory, size, alignment, allocator->context);
        return;
    }
    free(memory);
}

size_t maudLayoutAdd(maudLayout* layout, size_t count, size_t itemSize, size_t alignment)
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
    if (ckd_add(&end, offset, bytes))
    {
        layout->overflow = true;
        return 0;
    }
    layout->size = end;
#if MAUD_REDZONES
    // The gap spans a whole item past the granule the part ends in, so
    // that a read of a field of the item one past the end lands in it;
    // it is poisoned from the part's very end, which ASan marks within
    // the last granule as the bytes past the part.
    size_t gap = (end + 7u) & ~(size_t)7u;
    size_t span = itemSize > REDZONE_BYTES ? (itemSize + 7u) & ~(size_t)7u : REDZONE_BYTES;
    if (layout->gapCount < MAUD_LAYOUT_GAPS && !ckd_add(&layout->size, gap, span))
    {
        layout->gaps[layout->gapCount] = end;
        layout->spans[layout->gapCount++] = gap - end + span;
    }
    else
    {
        layout->size = end;
    }
#endif
    return offset;
}

void maudLayoutPoison(const maudLayout* layout, void* block)
{
#if MAUD_REDZONES
    for (uint32_t i = 0; block != nullptr && i < layout->gapCount; ++i)
    {
        ASAN_POISON_MEMORY_REGION((unsigned char*)block + layout->gaps[i], layout->spans[i]);
    }
#else
    (void)layout;
    (void)block;
#endif
}
