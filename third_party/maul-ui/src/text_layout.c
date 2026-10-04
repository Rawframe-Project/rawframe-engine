// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Measuring and painting text blocks (record mui-0006): the block and
// font a node's host key and text style name, shaped for the node's
// direction, broken into lines, and for painting reordered by UAX #9
// rules L1 and L2, aligned, and drawn as a glyph run per line and item.

#include "line_break.h"
#include "text_block.h"
#include "text_service.h"
#include "text_shape.h"

#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"
#include "maul-unicode/bidi.h"

#include <math.h>

// What laying out a node's block needs: the block, shaped, its font and
// the node's style, with the line scale and metrics in logical units.
typedef struct Paragraph
{
    muiTextService* service;
    muiTextBlock* block;
    const muiFont* font;
    uint64_t fontKey;
    muiComputedTextStyle style;
    muiLineScale scale;
    float lineHeight;
    // From a line's top to its baseline.
    float baseline;
    bool rtl;
} Paragraph;

static muiTextBlock* FindBlock(const muiTextService* service, uint64_t key)
{
    uint32_t slot = muiPoolResolve(&service->blocks.pool, (uint32_t)key, (uint32_t)(key >> 32));
    return slot != 0 ? &service->blocks.blocks[slot - 1] : nullptr;
}

// The font a key names, and its key, key 0 being the default font's.
// Sets up a node's paragraph; false when there is nothing to lay out or
// its shaping found no memory, which the service counts.
static bool Prepare(const muiTextHost* host, muiNodeId nodeId, uint64_t hostKey, Paragraph* out)
{
    if (host == nullptr || host->service == nullptr ||
        muiNode_GetTextStyle(host->context, nodeId, &out->style) != mui_success)
    {
        return false;
    }
    muiTextService* service = host->service;
    out->service = service;
    out->block = FindBlock(service, hostKey);
    out->font = muiFindFont(service, out->style.font, &out->fontKey);
    if (out->block == nullptr || out->font == nullptr)
    {
        return false;
    }
    out->rtl = muiNode_IsRightToLeft(host->context, nodeId);
    if (!muiShapeTextBlock(service, out->block, out->font, out->fontKey, out->rtl))
    {
        service->failures++;
        return false;
    }
    const muiFontMetrics* metrics = &out->font->metrics;
    float size = out->style.size;
    out->scale = (muiLineScale){size / (float)metrics->unitsPerEm, out->style.letterSpacing};
    float content = (metrics->ascent + metrics->descent) * size;
    out->lineHeight =
        out->style.automaticLineHeight ? content + metrics->lineGap * size : out->style.lineHeight;
    // Half the leading above the ascent, as CSS places a line's text.
    out->baseline = (out->lineHeight - content) * 0.5f + metrics->ascent * size;
    return true;
}

// Breaks a paragraph's lines into the service's scratch; false when
// there is no memory for them.
static bool Break(Paragraph* paragraph, muiBreakMode mode, float width, uint32_t* countOut)
{
    muiTextService* service = paragraph->service;
    const muiTextBlock* block = paragraph->block;
    // An opportunity per line, and the empty line after a last break.
    size_t capacity = (size_t)block->breakCount + 1u;
    if (!muiReserve(&service->allocator, &service->lines, capacity * sizeof(muiTextLine)))
    {
        service->failures++;
        return false;
    }
    *countOut = muiBreakLines(block, &paragraph->scale, mode, width, service->lines.data,
                              (uint32_t)capacity);
    return true;
}

static muiBreakMode ModeOf(const Paragraph* paragraph, muiMeasureMode mode)
{
    if (mode == mui_measureMinContent)
    {
        return mui_breakEvery;
    }
    if (mode == mui_measureMaxContent || paragraph->style.wrap == mui_textNoWrap)
    {
        return mui_breakMandatory;
    }
    return mui_breakWrap;
}

// A line's glyphs and width: the block's, or, when the line breaks where
// shaping is unsafe to break, its own from shaping it alone.
typedef struct LineGlyphs
{
    const muiTextItem* items;
    uint32_t itemCount;
    const muiShapedGlyph* glyphs;
    uint32_t glyphCount;
    float width;
} LineGlyphs;

// The width of the glyphs of clusters before end, with spacing after
// each cluster.
static float WidthBefore(const Paragraph* paragraph, const LineGlyphs* source, uint32_t end)
{
    float width = 0.0f;
    for (uint32_t k = 0; k < source->itemCount; k++)
    {
        const muiShapedGlyph* glyphs = source->glyphs + source->items[k].firstGlyph;
        uint32_t count = source->items[k].glyphCount;
        for (uint32_t i = 0; i < count; i++)
        {
            if (glyphs[i].cluster >= end)
            {
                continue;
            }
            width += (float)glyphs[i].advance * paragraph->scale.scale;
            if (i + 1 == count || glyphs[i + 1].cluster != glyphs[i].cluster)
            {
                width += paragraph->scale.spacing;
            }
        }
    }
    return width;
}

// A line's glyphs; false when shaping it alone found no memory, which the
// service counts. A line shaped alone takes its trailing white space
// with it, as a broken line is shaped.
static bool GlyphsOf(const Paragraph* paragraph, const muiTextLine* line, LineGlyphs* out)
{
    const muiTextBlock* block = paragraph->block;
    if (!muiIsBreakUnsafe(block, line->start) && !muiIsBreakUnsafe(block, line->next))
    {
        *out = (LineGlyphs){block->items.data, block->itemCount, block->glyphs.data,
                            block->glyphCount, line->width};
        return true;
    }
    muiTextService* service = paragraph->service;
    muiTextLineShape shape = {&service->lineItems, 0, &service->lineGlyphs, 0};
    if (!muiShapeTextLine(service, block, paragraph->font, line->start, line->next, &shape))
    {
        service->failures++;
        return false;
    }
    *out = (LineGlyphs){service->lineItems.data, shape.itemCount, service->lineGlyphs.data,
                        shape.glyphCount, 0.0f};
    out->width = WidthBefore(paragraph, out, line->end);
    return true;
}

muiSize muiMeasureText(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)height;
    Paragraph paragraph;
    uint32_t count = 0;
    if (!Prepare(user, nodeId, hostKey, &paragraph) ||
        !Break(&paragraph, ModeOf(&paragraph, width.mode), width.size, &count))
    {
        return (muiSize){0.0f, 0.0f};
    }
    const muiTextLine* lines = paragraph.service->lines.data;
    float widest = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        LineGlyphs glyphs;
        if (!GlyphsOf(&paragraph, &lines[i], &glyphs))
        {
            return (muiSize){0.0f, 0.0f};
        }
        widest = fmaxf(widest, glyphs.width);
    }
    return (muiSize){width.mode == mui_measureExact ? width.size : widest,
                     (float)count * paragraph.lineHeight};
}

// Where a line starts across a content box of the width.
static float Align(const Paragraph* paragraph, float lineWidth, float width)
{
    float room = width - lineWidth;
    switch (paragraph->style.align)
    {
    case mui_textAlignCenter:
        return room * 0.5f;
    case mui_textAlignEnd:
        return paragraph->rtl ? 0.0f : room;
    default:
        return paragraph->rtl ? room : 0.0f;
    }
}

// Draws the glyphs of an item whose clusters fall from start up to end,
// from pen x, and returns the pen after them.
// The first of count glyphs past those whose cluster is before offset:
// clusters rise in a left-to-right item and fall in a right-to-left one,
// so before means below, or above for a right-to-left item.
static uint32_t Seek(const muiShapedGlyph* glyphs, uint32_t count, uint32_t offset, bool rtl)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        bool before = rtl ? glyphs[middle].cluster >= offset : glyphs[middle].cluster < offset;
        if (before)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

static float PaintSegment(const Paragraph* paragraph, const LineGlyphs* source,
                          const muiTextItem* item, uint32_t start, uint32_t end, float pen,
                          float baseline, muiDrawSink* sink)
{
    const muiShapedGlyph* shaped = source->glyphs + item->firstGlyph;
    muiGlyph* glyphs = paragraph->service->glyphs.data;
    float scale = paragraph->scale.scale;
    // The glyphs of clusters from start up to end lie together: from the
    // first not before start to the first not before end, or for a
    // right-to-left item from the first below end to the first below
    // start.
    bool rtl = (item->level & 1) != 0;
    uint32_t first = Seek(shaped, item->glyphCount, rtl ? end : start, rtl);
    uint32_t last = Seek(shaped, item->glyphCount, rtl ? start : end, rtl);
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
        const muiGlyphRun run = {paragraph->fontKey, paragraph->style.size, paragraph->style.color,
                                 0.0f, baseline};
        (void)muiDrawSink_AddGlyphRun(sink, &run, glyphs, count);
    }
    return pen;
}

// Draws one bidi run of a line: its items left to right, which for an
// odd level is their logical order backwards.
static float PaintRun(const Paragraph* paragraph, const LineGlyphs* source, uint32_t start,
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

static void PaintLine(const Paragraph* paragraph, const muiTextLine* line, float width,
                      float baseline, muiDrawSink* sink)
{
    const muiTextBlock* block = paragraph->block;
    LineGlyphs source;
    if (!GlyphsOf(paragraph, line, &source))
    {
        return;
    }
    if (!muiReserve(&paragraph->service->allocator, &paragraph->service->glyphs,
                    ((size_t)source.glyphCount + 1u) * sizeof(muiGlyph)))
    {
        paragraph->service->failures++;
        return;
    }
    uint32_t length = line->end - line->start;
    size_t found = 0;
    const char* text = block->text.data;
    const uint8_t* levels = block->levels.data;
    if (muniReorderBidiLine(text + line->start, levels + line->start, length,
                            paragraph->rtl ? 1 : 0, paragraph->service->runs.data, length,
                            &found) != muni_success)
    {
        return;
    }
    const muniBidiRun* runs = paragraph->service->runs.data;
    float pen = Align(paragraph, source.width, width);
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
    Paragraph paragraph;
    if (!Prepare(user, nodeId, hostKey, &paragraph) || paragraph.block->length == 0)
    {
        return NAN;
    }
    return paragraph.baseline;
}

void muiPaintText(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    (void)height;
    Paragraph paragraph;
    uint32_t count = 0;
    if (!Prepare(user, nodeId, hostKey, &paragraph) ||
        !Break(&paragraph, ModeOf(&paragraph, mui_measureAtMost), width, &count))
    {
        return;
    }
    muiTextService* service = paragraph.service;
    const muiTextBlock* block = paragraph.block;
    if (!muiReserve(&service->allocator, &service->runs,
                    ((size_t)block->length + 1u) * sizeof(muniBidiRun)))
    {
        service->failures++;
        return;
    }
    const muiTextLine* lines = service->lines.data;
    for (uint32_t i = 0; i < count; i++)
    {
        float top = (float)i * paragraph.lineHeight;
        PaintLine(&paragraph, &lines[i], width, top + paragraph.baseline, sink);
    }
}
