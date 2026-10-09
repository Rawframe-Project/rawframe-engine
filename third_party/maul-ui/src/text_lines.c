// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A block's lines kept between layouts (record mui-0006). Lines never
// cross a mandatory break, and a paragraph's sums start from its own
// start, so its lines, their widths and their glyphs' widths are its
// own: an edit breaks again the paragraphs it reached, as shaping
// shapes them again. A block whose spans set run styles has lines of
// their own heights and is broken whole each time, as it is shaped.

#include "text_lines.h"

#include "text_service.h"

#include <math.h>
#include <string.h>

static bool Matches(const muiLineCache* cache, const muiTextBlock* block, const muiLineScale* scale,
                    muiBreakMode mode, float width)
{
    return cache->shaping == block->shapings && cache->size == scale->size &&
           cache->spacing == scale->spacing && (mode != mui_breakWrap || cache->width == width);
}

// The first of count lines starting at or past an offset.
static uint32_t LineFrom(const muiTextLine* lines, uint32_t count, uint32_t offset)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        low = lines[middle].start < offset ? middle + 1 : low;
        high = lines[middle].start < offset ? high : middle;
    }
    return low;
}

// Makes room for count lines at first in a cache whose lines from after
// on (tail of them) move there, by delta bytes.
static bool MakeRoom(const muiAllocator* allocator, muiLineCache* cache, uint32_t first,
                     uint32_t count, uint32_t after, uint32_t tail, int64_t delta)
{
    size_t total = (size_t)first + count + tail;
    if (!muiReserveKeeping(allocator, &cache->lines, (total + 1u) * sizeof(muiTextLine),
                           (size_t)cache->count * sizeof(muiTextLine)) ||
        !muiReserveKeeping(allocator, &cache->widths, (total + 1u) * sizeof(float),
                           (size_t)cache->count * sizeof(float)))
    {
        return false;
    }
    muiTextLine* lines = cache->lines.data;
    float* widths = cache->widths.data;
    if (tail != 0)
    {
        memmove(lines + first + count, lines + after, tail * sizeof(muiTextLine));
        memmove(widths + first + count, widths + after, tail * sizeof(float));
    }
    for (uint32_t i = first + count; i < total; i++)
    {
        lines[i].start = (uint32_t)((int64_t)lines[i].start + delta);
        lines[i].end = (uint32_t)((int64_t)lines[i].end + delta);
        lines[i].next = (uint32_t)((int64_t)lines[i].next + delta);
    }
    cache->count = (uint32_t)total;
    return true;
}

// Breaks again the cache's stale paragraphs, all of them when it was
// made for another shaping, line scale or width.
static bool Refresh(muiParagraph* paragraph, muiLineCache* cache, muiBreakMode mode, float width)
{
    const muiTextBlock* block = paragraph->block;
    muiTextService* service = paragraph->service;
    if (!Matches(cache, block, &paragraph->scale, mode, width))
    {
        *cache = (muiLineCache){cache->lines,
                                cache->widths,
                                0,
                                block->length,
                                block->shapings,
                                paragraph->scale.size,
                                paragraph->scale.spacing,
                                width,
                                0.0f,
                                {true, 0, block->length}};
    }
    if (!cache->stale.on)
    {
        return true;
    }
    uint32_t start = cache->stale.start;
    uint32_t end = cache->stale.end;
    int64_t delta = (int64_t)block->length - (int64_t)cache->length;
    size_t capacity = (size_t)muiCountBreaksWithin(block, start, end) + 1u;
    if (!muiReserve(&service->allocator, &service->lines, capacity * sizeof(muiTextLine)))
    {
        return false;
    }
    muiTextLine* region = service->lines.data;
    uint32_t count = muiBreakLinesWithin(block, &paragraph->scale, mode, width, start, end, region,
                                         (uint32_t)capacity);
    // The lines before the paragraphs, and those after, the text's empty
    // last line among them unless they reach the end, which breaks it.
    const muiTextLine* old = cache->lines.data;
    uint32_t first = LineFrom(old, cache->count, start);
    uint32_t after = end == block->length
                         ? cache->count
                         : LineFrom(old, cache->count, (uint32_t)((int64_t)end - delta));
    if (!MakeRoom(&service->allocator, cache, first, count, after, cache->count - after, delta))
    {
        return false;
    }
    muiTextLine* lines = cache->lines.data;
    float* widths = cache->widths.data;
    memcpy(lines + first, region, count * sizeof(muiTextLine));
    for (uint32_t i = first; i < first + count; i++)
    {
        muiLineGlyphs glyphs;
        if (!muiGetLineGlyphs(paragraph, &lines[i], &glyphs))
        {
            cache->shaping = 0;
            return false;
        }
        widths[i] = glyphs.width;
    }
    cache->widest = 0.0f;
    for (uint32_t i = 0; i < cache->count; i++)
    {
        cache->widest = fmaxf(cache->widest, widths[i]);
    }
    cache->length = block->length;
    cache->stale.on = false;
    return true;
}

// The cache of a mode, refreshed.
static muiLineCache* Kept(muiParagraph* paragraph, muiBreakMode mode, float width)
{
    muiLineCache* cache = &paragraph->block->lineCaches[mode];
    if (!Refresh(paragraph, cache, mode, width))
    {
        paragraph->service->failures++;
        return nullptr;
    }
    return cache;
}

bool muiLayLines(muiParagraph* paragraph, muiBreakMode mode, float width, uint32_t* countOut)
{
    if (paragraph->block->runStyleCount != 0)
    {
        return muiBreakParagraph(paragraph, mode, width, countOut);
    }
    muiTextService* service = paragraph->service;
    const muiLineCache* cache = Kept(paragraph, mode, width);
    if (cache == nullptr || !muiReserve(&service->allocator, &service->lines,
                                        ((size_t)cache->count + 1u) * sizeof(muiTextLine)))
    {
        service->failures += cache != nullptr ? 1u : 0u;
        return false;
    }
    muiTextLine* lines = service->lines.data;
    memcpy(lines, cache->lines.data, cache->count * sizeof(muiTextLine));
    // Every line as tall as the paragraph's, as muiBreakParagraph places
    // them with no run styles.
    for (uint32_t i = 0; i < cache->count; i++)
    {
        lines[i].top = (float)i * paragraph->lineHeight;
        lines[i].height = paragraph->lineHeight;
        lines[i].baseline = lines[i].top + paragraph->baseline;
    }
    *countOut = cache->count;
    return true;
}

bool muiMeasureLines(muiParagraph* paragraph, muiBreakMode mode, float width, float* widestOut,
                     float* heightOut)
{
    uint32_t count = 0;
    if (paragraph->block->runStyleCount != 0)
    {
        if (!muiBreakParagraph(paragraph, mode, width, &count))
        {
            return false;
        }
        const muiTextLine* lines = paragraph->service->lines.data;
        float widest = 0.0f;
        for (uint32_t i = 0; i < count; i++)
        {
            muiLineGlyphs glyphs;
            if (!muiGetLineGlyphs(paragraph, &lines[i], &glyphs))
            {
                return false;
            }
            widest = fmaxf(widest, glyphs.width);
        }
        *widestOut = widest;
        *heightOut = muiParagraphHeight(lines, count);
        return true;
    }
    const muiLineCache* cache = Kept(paragraph, mode, width);
    if (cache == nullptr)
    {
        return false;
    }
    *widestOut = cache->widest;
    // As the last line's top and height add up.
    *heightOut = cache->count != 0
                     ? (float)(cache->count - 1u) * paragraph->lineHeight + paragraph->lineHeight
                     : 0.0f;
    return true;
}
