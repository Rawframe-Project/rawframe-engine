// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Greedy line breaking (record mui-0006). A line takes opportunities
// while its visible text fits; one that does not fit even alone
// overflows. A width read back from layout may differ from the measured
// one in its last bits, so a line fits within a 256th of a unit.

#include "line_break.h"

#include <stdbool.h>

enum
{
    // How far a line may pass the width and still fit: 1/256 unit.
    SLACK_SHIFT = 8
};

float muiTextWidth(const muiTextBlock* block, const muiLineScale* scale, uint32_t start,
                   uint32_t end)
{
    const double* advances = block->advances.data;
    const uint32_t* clusters = block->clusters.data;
    return (float)((advances[end] - advances[start]) * (double)scale->size) +
           (float)(clusters[end] - clusters[start]) * scale->spacing;
}

// Whether the bytes before end are a line break (LF, VT, FF, CR, NEL,
// LS or PS), and how many.
static uint32_t BreakBefore(const unsigned char* text, uint32_t start, uint32_t end)
{
    if (end - start >= 1 && text[end - 1] >= 0x0A && text[end - 1] <= 0x0D)
    {
        return 1;
    }
    if (end - start >= 2 && text[end - 2] == 0xC2 && text[end - 1] == 0x85)
    {
        return 2;
    }
    if (end - start >= 3 && text[end - 3] == 0xE2 && text[end - 2] == 0x80 &&
        (text[end - 1] == 0xA8 || text[end - 1] == 0xA9))
    {
        return 3;
    }
    return 0;
}

// Where a line's visible text ends: before its line break, if any, and
// before the spaces and tabs that hang after it. CR LF is one break.
static uint32_t VisibleEnd(const unsigned char* text, uint32_t start, uint32_t end)
{
    uint32_t at = end - BreakBefore(text, start, end);
    if (at < end && text[at] == '\n' && at > start && text[at - 1] == '\r')
    {
        at--;
    }
    while (at > start && (text[at - 1] == ' ' || text[at - 1] == '\t'))
    {
        at--;
    }
    return at;
}

typedef struct Lines
{
    const muiTextBlock* block;
    const muiLineScale* scale;
    muiTextLine* lines;
    uint32_t capacity;
    uint32_t count;
} Lines;

static void Emit(Lines* out, uint32_t start, uint32_t next)
{
    if (out->count < out->capacity)
    {
        uint32_t end = VisibleEnd(out->block->text.data, start, next);
        out->lines[out->count] =
            (muiTextLine){start, end, next, muiTextWidth(out->block, out->scale, start, end)};
    }
    out->count++;
}

uint32_t muiBreakLines(const muiTextBlock* block, const muiLineScale* scale, muiBreakMode mode,
                       float width, muiTextLine* lines, uint32_t capacity)
{
    Lines out = {block, scale, lines, capacity, 0};
    const muiTextBreak* breaks = block->breaks.data;
    const unsigned char* text = block->text.data;
    float limit = width + 1.0f / (float)(1 << SLACK_SHIFT);
    uint32_t start = 0;
    // The last opportunity the line fits up to, when there is one.
    uint32_t fit = 0;
    bool fits = false;
    for (uint32_t i = 0; i < block->breakCount;)
    {
        uint32_t offset = breaks[i].offset;
        bool mandatory = breaks[i].mandatory != 0;
        if (mode == mui_breakWrap && fits &&
            muiTextWidth(block, scale, start, VisibleEnd(text, start, offset)) > limit)
        {
            Emit(&out, start, fit);
            start = fit;
            fits = false;
            continue;
        }
        if (mandatory || mode == mui_breakEvery)
        {
            Emit(&out, start, offset);
            start = offset;
            fits = false;
        }
        else
        {
            fit = offset;
            fits = true;
        }
        i++;
    }
    // A last line break leaves an empty line after it.
    if (block->length != 0 && BreakBefore(text, 0, block->length) != 0)
    {
        Emit(&out, block->length, block->length);
    }
    return out.count;
}
