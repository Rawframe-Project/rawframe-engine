// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Measuring and painting text blocks (record mui-0006): a node's
// paragraph broken into lines, and for painting reordered by UAX #9
// rules L1 and L2, aligned, and drawn as a glyph run per line and item,
// an input method's composition underlined after.

#include "text_boxes.h"
#include "text_paragraph.h"

#include "maul-ui/text_block.h"
#include "maul-unicode/bidi.h"

#include <math.h>

muiSize muiMeasureText(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)height;
    muiParagraph paragraph;
    uint32_t count = 0;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &paragraph) ||
        !muiBreakParagraph(&paragraph, muiParagraphBreakMode(&paragraph, width.mode), width.size,
                           &count))
    {
        return (muiSize){0.0f, 0.0f};
    }
    const muiTextLine* lines = paragraph.service->lines.data;
    float widest = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        muiLineGlyphs glyphs;
        if (!muiGetLineGlyphs(&paragraph, &lines[i], &glyphs))
        {
            return (muiSize){0.0f, 0.0f};
        }
        widest = fmaxf(widest, glyphs.width);
    }
    return (muiSize){width.mode == mui_measureExact ? width.size : widest,
                     (float)count * paragraph.lineHeight};
}

static float PaintSegment(const muiParagraph* paragraph, const muiLineGlyphs* source,
                          const muiTextItem* item, uint32_t start, uint32_t end, float pen,
                          float baseline, muiDrawSink* sink)
{
    const muiShapedGlyph* shaped = source->glyphs + item->firstGlyph;
    muiGlyph* glyphs = paragraph->service->glyphs.data;
    float scale = muiItemScale(paragraph, item);
    uint32_t first = 0;
    uint32_t last = 0;
    muiSegmentGlyphs(shaped, item, start, end, &first, &last);
    uint32_t count = 0;
    for (uint32_t i = first; i < last; i++)
    {
        const muiShapedGlyph* glyph = &shaped[i];
        // y down; the integer is negated, so no -0 enters the list.
        glyphs[count++] = (muiGlyph){glyph->id, pen + (float)glyph->offsetX * scale,
                                     (float)-glyph->offsetY * scale};
        pen += (float)glyph->advance * scale;
        // Spacing follows each cluster, in visual order.
        if (i + 1 == item->glyphCount || shaped[i + 1].cluster != glyph->cluster)
        {
            pen += paragraph->scale.spacing;
        }
    }
    if (count != 0)
    {
        const muiGlyphRun run = {paragraph->chain.keys[item->face], paragraph->style.size,
                                 paragraph->style.color, 0.0f, baseline};
        (void)muiDrawSink_AddGlyphRun(sink, &run, glyphs, count);
    }
    return pen;
}

// Draws one bidi run of a line: its items left to right, which for an
// odd level is their logical order backwards.
static float PaintRun(const muiParagraph* paragraph, const muiLineGlyphs* source, uint32_t start,
                      uint32_t end, bool odd, float pen, float baseline, muiDrawSink* sink)
{
    const muiTextItem* items = source->items;
    uint32_t count = source->itemCount;
    for (uint32_t k = 0; k < count; k++)
    {
        const muiTextItem* item = &items[odd ? count - 1 - k : k];
        if (item->end <= start || item->start >= end)
        {
            continue;
        }
        uint32_t from = item->start > start ? item->start : start;
        uint32_t to = item->end < end ? item->end : end;
        pen = PaintSegment(paragraph, source, item, from, to, pen, baseline, sink);
    }
    return pen;
}

static void PaintLine(const muiParagraph* paragraph, const muiTextLine* line, float width,
                      float baseline, muiDrawSink* sink)
{
    muiLineGlyphs source;
    if (!muiGetLineGlyphs(paragraph, line, &source))
    {
        return;
    }
    if (!muiReserve(&paragraph->service->allocator, &paragraph->service->glyphs,
                    ((size_t)source.glyphCount + 1u) * sizeof(muiGlyph)))
    {
        paragraph->service->failures++;
        return;
    }
    size_t found = 0;
    if (!muiReorderLine(paragraph, line, &found))
    {
        return;
    }
    const muniBidiRun* runs = paragraph->service->runs.data;
    float pen = muiAlignLine(paragraph, source.width, width);
    for (size_t i = 0; i < found; i++)
    {
        uint32_t start = line->start + (uint32_t)runs[i].start;
        pen = PaintRun(paragraph, &source, start, start + (uint32_t)runs[i].length,
                       (runs[i].level & 1) != 0, pen, baseline, sink);
    }
}

float muiTextBaseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height)
{
    (void)width;
    (void)height;
    muiParagraph paragraph;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &paragraph) || paragraph.block->length == 0)
    {
        return NAN;
    }
    return paragraph.baseline;
}

// Underlines a block's composition on a line: each segment's stretches,
// or the whole composition's, at the first font's underline position, the
// target twice as thick.
static void PaintComposition(const muiLaidText* laid, uint32_t index, muiDrawSink* sink)
{
    const muiParagraph* paragraph = &laid->paragraph;
    const muiTextBlock* block = paragraph->block;
    muiTextBoxes boxes;
    if (!muiGetLineBoxes(laid, index, &boxes))
    {
        return;
    }
    // A font without an underline gets CSS's usual one.
    const muiFontMetrics* metrics = &paragraph->chain.fonts[0]->metrics;
    bool given = metrics->underlineThickness > 0.0f;
    float size = paragraph->style.size;
    float thin = (given ? metrics->underlineThickness : 0.05f) * size;
    float y = (float)index * paragraph->lineHeight + paragraph->baseline +
              (given ? metrics->underlineOffset : 0.1f) * size;
    const muiCompositionSegment whole = {0, block->compositionLength, mui_compositionUnderline};
    const muiCompositionSegment* segments =
        block->segmentCount != 0 ? block->segments.data : &whole;
    uint32_t count = block->segmentCount != 0 ? block->segmentCount : 1;
    for (uint32_t k = 0; k < count; k++)
    {
        uint32_t start = block->compositionStart + segments[k].start;
        float height = segments[k].style == mui_compositionTarget ? thin * 2.0f : thin;
        uint32_t at = 0;
        float left = 0.0f;
        float right = 0.0f;
        while (segments[k].style != mui_compositionPlain &&
               muiNextStretch(&boxes, &at, start, start + segments[k].length, &left, &right))
        {
            (void)muiDrawSink_AddRect(sink, (muiRect){left, y, right - left, height},
                                      paragraph->style.color);
        }
    }
}

void muiPaintText(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    (void)height;
    muiParagraph paragraph;
    uint32_t count = 0;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &paragraph) ||
        !muiBreakParagraph(&paragraph, muiParagraphBreakMode(&paragraph, mui_measureAtMost), width,
                           &count))
    {
        return;
    }
    muiTextService* service = paragraph.service;
    const muiTextLine* lines = service->lines.data;
    for (uint32_t i = 0; i < count; i++)
    {
        float top = (float)i * paragraph.lineHeight;
        PaintLine(&paragraph, &lines[i], width, top + paragraph.baseline, sink);
    }
    if (paragraph.block->compositionLength == 0)
    {
        return;
    }
    const muiLaidText laid = {paragraph, lines, count, width};
    for (uint32_t i = 0; i < count; i++)
    {
        PaintComposition(&laid, i, sink);
    }
}