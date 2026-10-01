// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks (record mui-0006): a block's text, what was found in it
// when it was set (line break opportunities and script runs), and its
// shaping for one font and direction, kept until either changes.

#ifndef MAUL_UI_SRC_TEXT_BLOCK_H
#define MAUL_UI_SRC_TEXT_BLOCK_H

#include "pool.h"

#include "maul-ui/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Memory from an allocator that grows to what is asked, its old contents
// dropped.
typedef struct muiBuffer
{
    void* data;
    size_t bytes;
} muiBuffer;

// Whether buffer holds at least bytes; false, keeping it, when memory
// runs out. Its first kept bytes stay as they were; the rest are
// undefined.
bool muiReserveKeeping(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes, size_t kept);

// As muiReserveKeeping, keeping nothing.
bool muiReserve(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes);

void muiFreeBuffer(const muiAllocator* allocator, muiBuffer* buffer);

// A line break opportunity before the byte at offset.
typedef struct muiTextBreak
{
    uint32_t offset;
    uint32_t mandatory;
} muiTextBreak;

// A script run ending before the byte at end; script is an ISO 15924
// tag, as HarfBuzz's.
typedef struct muiTextScript
{
    uint32_t end;
    uint32_t script;
} muiTextScript;

// A glyph as HarfBuzz shaped it, in font units: its id, the byte offset
// of its cluster, its advance and its offset, y up.
typedef struct muiShapedGlyph
{
    uint32_t id;
    uint32_t cluster;
    int32_t advance;
    int32_t offsetX;
    int32_t offsetY;
} muiShapedGlyph;

// Bytes of one bidi level and one script, and their glyphs, in visual
// order.
typedef struct muiTextItem
{
    uint32_t start;
    uint32_t end;
    uint32_t firstGlyph;
    uint32_t glyphCount;
    uint32_t level;
    uint32_t script;
} muiTextItem;

typedef struct muiTextBlock
{
    // The text, length bytes.
    muiBuffer text;
    uint32_t length;
    // muiTextBreak, the end of the text included when it is not empty.
    muiBuffer breaks;
    uint32_t breakCount;
    // muiTextScript.
    muiBuffer scripts;
    uint32_t scriptCount;
    // The shaping, valid when shaped, for shapedFont and shapedRtl.
    bool shaped;
    bool shapedRtl;
    uint64_t shapedFont;
    // A bidi level per byte.
    muiBuffer levels;
    // muiTextItem, in logical order.
    muiBuffer items;
    uint32_t itemCount;
    // muiShapedGlyph.
    muiBuffer glyphs;
    uint32_t glyphCount;
    // A byte per byte: 1 where HarfBuzz marks the cluster starting there
    // unsafe to break.
    muiBuffer unsafe;
    // length + 1 sums from the start of the text: of advances in font
    // units (int64_t), and of clusters (uint32_t).
    muiBuffer advances;
    muiBuffer clusters;
} muiTextBlock;

typedef struct muiTextBlockStore
{
    muiPool pool;
    // Block i is blocks[i - 1].
    muiTextBlock* blocks;
} muiTextBlockStore;

// Frees what a block holds and zeroes it.
void muiReleaseTextBlock(const muiAllocator* allocator, muiTextBlock* block);

#endif // MAUL_UI_SRC_TEXT_BLOCK_H
