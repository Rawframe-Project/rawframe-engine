// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Measuring and painting text blocks (record mui-0006): a node's
// paragraph broken into lines, and for painting reordered by UAX #9
// rules L1 and L2, aligned, and drawn as a glyph run per line, item and
// stretch of one ink (the color and decorations the node's style and the
// block's spans give), an input method's composition underlined after.

#include "text_boxes.h"
#include "text_lines.h"
#include "text_paragraph.h"
#include "text_runs.h"

#include "maul-ui/text_block.h"
#include "maul-unicode/bidi.h"

#include <math.h>

muiSize muiMeasureText(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)height;
    muiParagraph paragraph;
    float widest = 0.0f;
    float tall = 0.0f;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &paragraph) ||
        !muiMeasureLines(&paragraph, muiParagraphBreakMode(&paragraph, width.mode), width.size,
                         &widest, &tall))
    {
        return (muiSize){0.0f, 0.0f};
    }
    return (muiSize){width.mode == mui_measureExact ? width.size : widest, tall};
}

// What paints a stretch of text.
typedef struct Ink
{
    muiColor color;
    muiTextDecoration decoration;
    muiColor decorationColor;
} Ink;

// The ink at a byte, from the node's style and the spans over it in
// order, and the stretch around the byte it holds over: where no span
// starts or ends.
static Ink InkAt(const muiParagraph* paragraph, uint32_t at, uint32_t* firstOut, uint32_t* endOut)
{
    Ink ink = {paragraph->style.color, paragraph->style.decoration,
               paragraph->style.decorationColor};
    const muiTextBlock* block = paragraph->block;
    const muiTextSpan* spans = block->spans.data;
    uint32_t first = 0;
    uint32_t end = UINT32_MAX;
    for (uint32_t i = 0; i < block->spanCount; i++)
    {
        const muiTextSpan* span = &spans[i];
        uint32_t spanEnd = span->start + span->length;
        if (at < span->start)
        {
            end = span->start < end ? span->start : end;
            continue;
        }
        if (at >= spanEnd)
        {
            first = spanEnd > first ? spanEnd : first;
            continue;
        }
        first = span->start > first ? span->start : first;
        end = spanEnd < end ? spanEnd : end;
        if ((span->mask & MUI_PROPERTY_BIT(mui_propertyTextColor)) != 0)
        {
            ink.color = span->style.color;
        }
        if ((span->mask & MUI_PROPERTY_BIT(mui_propertyTextDecoration)) != 0)
        {
            ink.decoration = span->style.decoration;
        }
        if ((span->mask & MUI_PROPERTY_BIT(mui_propertyTextDecorationColor)) != 0)
        {
            ink.decorationColor = span->style.decorationColor;
        }
    }
    *firstOut = first;
    *endOut = end;
    return ink;
}

// Where a decoration lies under, over or through a line, and how thick:
// from a font's metrics at a size, or CSS's usual ones for a font without
// them.
static muiRect DecorationRect(const muiFontMetrics* metrics, float size, muiTextDecoration line,
                              float left, float right, float baseline)
{
    bool underline = metrics->underlineThickness > 0.0f;
    float thin = (underline ? metrics->underlineThickness : 0.05f) * size;
    float y = baseline + (underline ? metrics->underlineOffset : 0.1f) * size;
    if (line == mui_decorationOverline)
    {
        y = baseline - metrics->ascent * size;
    }
    else if (line == mui_decorationLineThrough)
    {
        bool strikeout = metrics->strikeoutThickness > 0.0f;
        thin = strikeout ? metrics->strikeoutThickness * size : thin;
        y = baseline - (strikeout ? metrics->strikeoutOffset : 0.3f) * size;
    }
    return (muiRect){left, y, right - left, thin};
}

// Draws an item's decorations: in the node's first font for its own
// text, as CSS's first available font, and in its own for a span's run.
static void Decorate(const muiParagraph* paragraph, const muiTextItem* item, const Ink* ink,
                     muiTextDecoration lines, float left, float right, float baseline,
                     muiDrawSink* sink)
{
    const muiFont* font = paragraph->chain.fonts[item->style != 0 ? item->face : 0];
    float size = paragraph->style.size * item->scale;
    const muiColor color = ink->decorationColor.a > 0.0f ? ink->decorationColor : ink->color;
    const muiTextDecoration each[3] = {mui_decorationUnderline, mui_decorationOverline,
                                       mui_decorationLineThrough};
    for (int i = 0; i < 3; i++)
    {
        if ((ink->decoration & lines & each[i]) != 0 && right > left)
        {
            (void)muiDrawSink_AddRect(
                sink, DecorationRect(&font->metrics, size, each[i], left, right, baseline), color);
        }
    }
}

// Paints the glyphs of an item from start up to end, in one ink, from a
// pen: underline and overline before them, line-through after, as CSS
// paints them. Returns the pen after.
static float PaintSegment(const muiParagraph* paragraph, const muiLineGlyphs* source,
                          const muiTextItem* item, uint32_t start, uint32_t end, const Ink* ink,
                          float pen, float baseline, muiDrawSink* sink)
{
    // A span's shift raises its glyphs and their decorations.
    baseline -= muiRunShift(paragraph->block, item->style);
    float left = pen;
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
    Decorate(paragraph, item, ink, mui_decorationUnderline | mui_decorationOverline, left, pen,
             baseline, sink);
    if (count != 0)
    {
        const muiGlyphRun run = {paragraph->chain.keys[item->face],
                                 paragraph->style.size * item->scale, ink->color, 0.0f, baseline};
        (void)muiDrawSink_AddGlyphRun(sink, &run, glyphs, count);
    }
    Decorate(paragraph, item, ink, mui_decorationLineThrough, left, pen, baseline, sink);
    return pen;
}

// Draws the bytes of an item from start up to end left to right, a
// stretch of one ink at a time: for an odd level, from the end backwards.
static float PaintItem(const muiParagraph* paragraph, const muiLineGlyphs* source,
                       const muiTextItem* item, uint32_t start, uint32_t end, bool odd, float pen,
                       float baseline, muiDrawSink* sink)
{
    uint32_t first = 0;
    uint32_t last = 0;
    if (odd)
    {
        for (uint32_t at = end; at > start;)
        {
            const Ink ink = InkAt(paragraph, at - 1, &first, &last);
            uint32_t low = first > start ? first : start;
            pen = PaintSegment(paragraph, source, item, low, at, &ink, pen, baseline, sink);
            at = low;
        }
        return pen;
    }
    for (uint32_t at = start; at < end;)
    {
        const Ink ink = InkAt(paragraph, at, &first, &last);
        uint32_t high = last < end ? last : end;
        pen = PaintSegment(paragraph, source, item, at, high, &ink, pen, baseline, sink);
        at = high;
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
        pen = PaintItem(paragraph, source, item, from, to, odd, pen, baseline, sink);
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
    (void)height;
    muiParagraph paragraph;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &paragraph) || paragraph.block->length == 0)
    {
        return NAN;
    }
    // Spans may make the first line taller: its own baseline, broken at
    // the width.
    uint32_t count = 0;
    if (paragraph.block->runStyleCount == 0 ||
        !muiBreakParagraph(&paragraph, muiParagraphBreakMode(&paragraph, mui_measureAtMost), width,
                           &count) ||
        count == 0)
    {
        return paragraph.baseline;
    }
    return ((const muiTextLine*)paragraph.service->lines.data)[0].baseline;
}

// Underlines a block's composition on a line: each segment's stretches,
// or the whole composition's, where an underline goes, the target twice
// as thick.
static void PaintComposition(const muiLaidText* laid, uint32_t index, muiDrawSink* sink)
{
    const muiParagraph* paragraph = &laid->paragraph;
    const muiTextBlock* block = paragraph->block;
    muiTextBoxes boxes;
    if (!muiGetLineBoxes(laid, index, &boxes))
    {
        return;
    }
    float baseline = laid->lines[index].baseline;
    const muiCompositionSegment whole = {0, block->compositionLength, mui_compositionUnderline};
    const muiCompositionSegment* segments =
        block->segmentCount != 0 ? block->segments.data : &whole;
    uint32_t count = block->segmentCount != 0 ? block->segmentCount : 1;
    for (uint32_t k = 0; k < count; k++)
    {
        uint32_t start = block->compositionStart + segments[k].start;
        uint32_t at = 0;
        float left = 0.0f;
        float right = 0.0f;
        while (segments[k].style != mui_compositionPlain &&
               muiNextStretch(&boxes, &at, start, start + segments[k].length, &left, &right))
        {
            muiRect rect =
                DecorationRect(&paragraph->chain.fonts[0]->metrics, paragraph->style.size,
                               mui_decorationUnderline, left, right, baseline);
            rect.height *= segments[k].style == mui_compositionTarget ? 2.0f : 1.0f;
            (void)muiDrawSink_AddRect(sink, rect, paragraph->style.color);
        }
    }
}

enum
{
    // A paragraph of more lines than this paints only those that can be
    // seen; one of fewer paints them all and is copied while it can be.
    CULLED_LINES = 64
};

// The lines from first up to end that meet what can be seen, and one
// more each way, as glyphs may reach past their line; lines go down the
// paragraph in order.
static void SeenLines(const muiTextLine* lines, uint32_t count, const muiRect* seen,
                      uint32_t* first, uint32_t* end)
{
    if (seen->width <= 0.0f || seen->height <= 0.0f)
    {
        *first = 0;
        *end = 0;
        return;
    }
    uint32_t low = 0;
    while (low < count && lines[low].top + lines[low].height < seen->y)
    {
        low++;
    }
    uint32_t high = low;
    while (high < count && lines[high].top <= seen->y + seen->height)
    {
        high++;
    }
    *first = low > 0 ? low - 1 : 0;
    *end = high < count ? high + 1 : count;
}

void muiPaintText(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    muiLaidText laid;
    uint32_t count = 0;
    if (!muiPrepareParagraph(user, nodeId, hostKey, &laid.paragraph) ||
        !muiLayLines(&laid.paragraph, muiParagraphBreakMode(&laid.paragraph, mui_measureAtMost),
                     width, &count))
    {
        return;
    }
    laid.lines = laid.paragraph.service->lines.data;
    laid.lineCount = count;
    laid.width = width;
    // An editing block is drawn scrolled to its caret.
    muiFollowCaret(&laid, height);
    const muiTextLine* lines = laid.lines;
    uint32_t first = 0;
    uint32_t end = count;
    muiRect seen;
    if (count > CULLED_LINES && muiDrawSink_GetVisibleRect(sink, &seen) == mui_success)
    {
        SeenLines(lines, count, &seen, &first, &end);
    }
    for (uint32_t i = first; i < end; i++)
    {
        PaintLine(&laid.paragraph, &lines[i], width, lines[i].baseline, sink);
    }
    for (uint32_t i = first; laid.paragraph.block->compositionLength != 0 && i < end; i++)
    {
        PaintComposition(&laid, i, sink);
    }
}