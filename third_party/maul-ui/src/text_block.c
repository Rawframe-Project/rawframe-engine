// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text block records and the buffers they grow (record mui-0006).

#include "text_block.h"

#include "allocator.h"

#include <stdalign.h>
#include <stddef.h>
#include <string.h>

bool muiReserve(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes)
{
    return muiReserveKeeping(allocator, buffer, bytes, 0);
}

bool muiReserveKeeping(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes, size_t kept)
{
    if (bytes <= buffer->bytes)
    {
        return true;
    }
    // Grows by half again, so a buffer that keeps growing costs linear
    // time in all.
    size_t grown = buffer->bytes + buffer->bytes / 2;
    size_t size = bytes > grown ? bytes : grown;
    void* data = muiAllocate(allocator, size, alignof(max_align_t));
    if (data == nullptr)
    {
        return false;
    }
    if (kept != 0)
    {
        memcpy(data, buffer->data, kept);
    }
    muiFreeBuffer(allocator, buffer);
    *buffer = (muiBuffer){data, size};
    return true;
}

void muiFreeBuffer(const muiAllocator* allocator, muiBuffer* buffer)
{
    if (buffer->data != nullptr)
    {
        muiRelease(allocator, buffer->data, buffer->bytes, alignof(max_align_t));
    }
    *buffer = (muiBuffer){0};
}

void muiReleaseTextBlock(const muiAllocator* allocator, muiTextBlock* block)
{
    muiBuffer* buffers[] = {&block->text,  &block->breaks, &block->scripts,  &block->levels,
                            &block->items, &block->glyphs, &block->advances, &block->clusters};
    for (size_t i = 0; i < sizeof buffers / sizeof buffers[0]; i++)
    {
        muiFreeBuffer(allocator, buffers[i]);
    }
    *block = (muiTextBlock){0};
}
