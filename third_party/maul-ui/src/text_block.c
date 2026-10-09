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

// The end of the paragraph a text has at from: past its mandatory break
// (UAX #14's BK, CR, LF and NL: VT, FF, LS and PS, CR, LF, CR LF and
// NEL), or the text's end.
uint32_t muiParagraphEnd(const char* text, uint32_t length, uint32_t from)
{
    const unsigned char* bytes = (const unsigned char*)text;
    for (uint32_t i = from; i < length; i++)
    {
        unsigned char c = bytes[i];
        if (c == '\n' || c == '\v' || c == '\f')
        {
            return i + 1;
        }
        if (c == '\r')
        {
            return i + 1 < length && bytes[i + 1] == '\n' ? i + 2 : i + 1;
        }
        if (c == 0xC2 && i + 1 < length && bytes[i + 1] == 0x85)
        {
            return i + 2;
        }
        if (c == 0xE2 && i + 2 < length && bytes[i + 1] == 0x80 &&
            (bytes[i + 2] == 0xA8 || bytes[i + 2] == 0xA9))
        {
            return i + 3;
        }
    }
    return length;
}

void muiReleaseTextBlock(const muiAllocator* allocator, muiTextBlock* block)
{
    muiBuffer* buffers[] = {
        &block->text,      &block->breaks, &block->scripts,         &block->levels,
        &block->items,     &block->glyphs, &block->advances,        &block->clusters,
        &block->unsafe,    &block->faces,  &block->segments,        &block->spans,
        &block->runStyles, &block->runs,   &block->editing.entries, &block->editing.bytes};
    for (size_t i = 0; i < sizeof buffers / sizeof buffers[0]; i++)
    {
        muiFreeBuffer(allocator, buffers[i]);
    }
    for (int i = 0; i < MUI_LINE_CACHES; i++)
    {
        muiFreeBuffer(allocator, &block->lineCaches[i].lines);
        muiFreeBuffer(allocator, &block->lineCaches[i].widths);
    }
    if (block->mask != nullptr)
    {
        muiReleaseTextBlock(allocator, block->mask);
        muiRelease(allocator, block->mask, sizeof *block->mask, alignof(muiTextBlock));
    }
    *block = (muiTextBlock){0};
}
