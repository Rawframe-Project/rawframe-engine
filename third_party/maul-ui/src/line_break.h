// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Breaking a shaped text block into lines (record mui-0006): greedy, at
// line break opportunities, from the block's sums of advances, so that
// measuring and painting break the same lines at the same width.

#ifndef MAUL_UI_SRC_LINE_BREAK_H
#define MAUL_UI_SRC_LINE_BREAK_H

#include "text_block.h"

#include <stdint.h>

// Where lines break.
typedef uint8_t muiBreakMode;

enum
{
    // Where the width runs out, and where the text must break.
    mui_breakWrap = 0,
    // Only where the text must break.
    mui_breakMandatory = 1,
    // At every opportunity.
    mui_breakEvery = 2,
};

// What a line's width is made of: advances in ems times the size, plus
// spacing after each cluster.
typedef struct muiLineScale
{
    float size;
    float spacing;
} muiLineScale;

// A line: bytes from start up to end, where its visible text ends (white
// space after it hangs), and next, where the next line starts.
typedef struct muiTextLine
{
    uint32_t start;
    uint32_t end;
    uint32_t next;
    float width;
    // Where the line lies down the paragraph: its top, its height and its
    // baseline, from the paragraph's top (src/text_paragraph.c).
    float top;
    float height;
    float baseline;
} muiTextLine;

// The width of bytes from start up to end.
float muiTextWidth(const muiTextBlock* block, const muiLineScale* scale, uint32_t start,
                   uint32_t end);

// Breaks a shaped block into lines no wider than width where it can;
// writes up to capacity of them into lines, which may be NULL when
// capacity is 0, and returns how many there are. A text that ends in a
// line break has an empty last line; an empty text has none.
uint32_t muiBreakLines(const muiTextBlock* block, const muiLineScale* scale, muiBreakMode mode,
                       float width, muiTextLine* lines, uint32_t capacity);

// muiBreakLines for the whole paragraphs from from up to to alone; the
// empty last line is theirs when to is the text's end.
uint32_t muiBreakLinesWithin(const muiTextBlock* block, const muiLineScale* scale,
                             muiBreakMode mode, float width, uint32_t from, uint32_t to,
                             muiTextLine* lines, uint32_t capacity);

// How many break opportunities a block has past from up to to.
uint32_t muiCountBreaksWithin(const muiTextBlock* block, uint32_t from, uint32_t to);

#endif // MAUL_UI_SRC_LINE_BREAK_H
