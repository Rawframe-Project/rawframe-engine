// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Paragraphs (record mui-0006): the block and font chain a node's host
// key and text style name, shaped for the node's direction, broken into
// lines, each line's glyphs, alignment and visual runs, as measuring,
// painting and hit testing share them.

#include "text_paragraph.h"

#include "maul-ui/text_style.h"
#include "maul-unicode/bidi.h"

static muiTextBlock* FindBlock(const muiTextService* service, uint64_t key)
{
    uint32_t slot = muiPoolResolve(&service->blocks.pool, (uint32_t)key, (uint32_t)(key >> 32));
    return slot != 0 ? &service->blocks.blocks[slot - 1] : nullptr;
}

// Sets up a node's paragraph; false when there is nothing to lay out or
// its shaping found no memory, which the service counts.
bool muiPrepareParagraph(const muiTextHost* host, muiNodeId nodeId, uint64_t hostKey,
                         muiParagraph* out)
{
    if (host == nullptr || host->service == nullptr ||
        muiNode_GetTextStyle(host->context, nodeId, &out->style) != mui_success)
    {
        return false;
    }
    muiTextService* service = host->service;
    out->service = service;
    out->block = FindBlock(service, hostKey);
    if (out->block == nullptr || !muiBuildChain(service, &out->style, &out->chain))
    {
        return false;
    }
    out->rtl = muiNode_IsRightToLeft(host->context, nodeId);
    if (!muiShapeTextBlock(service, out->block, &out->chain, out->rtl))
    {
        service->failures++;
        return false;
    }
    const muiFontMetrics* metrics = &out->chain.fonts[0]->metrics;
    float size = out->style.size;
    out->scale = (muiLineScale){size, out->style.letterSpacing};
    float content = (metrics->ascent + metrics->descent) * size;
    out->lineHeight =
        out->style.automaticLineHeight ? content + metrics->lineGap * size : out->style.lineHeight;
    // Half the leading above the ascent, as CSS places a line's text.
    out->baseline = (out->lineHeight - content) * 0.5f + metrics->ascent * size;
    return true;
}

// Breaks a paragraph's lines into the service's scratch; false when
// there is no memory for them.
bool muiBreakParagraph(muiParagraph* paragraph, muiBreakMode mode, float width, uint32_t* countOut)
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

muiBreakMode muiParagraphBreakMode(const muiParagraph* paragraph, muiMeasureMode mode)
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

// Logical units per unit of an item's font.
float muiItemScale(const muiParagraph* paragraph, const muiTextItem* item)
{
    return paragraph->scale.size / (float)item->units;
}

// The width of the glyphs of clusters before end, with spacing after
// each cluster.
static float WidthBefore(const muiParagraph* paragraph, const muiLineGlyphs* source, uint32_t end)
{
    float width = 0.0f;
    for (uint32_t k = 0; k < source->itemCount; k++)
    {
        const muiShapedGlyph* glyphs = source->glyphs + source->items[k].firstGlyph;
        uint32_t count = source->items[k].glyphCount;
        float scale = muiItemScale(paragraph, &source->items[k]);
        for (uint32_t i = 0; i < count; i++)
        {
            if (glyphs[i].cluster >= end)
            {
                continue;
            }
            width += (float)glyphs[i].advance * scale;
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
bool muiGetLineGlyphs(const muiParagraph* paragraph, const muiTextLine* line, muiLineGlyphs* out)
{
    const muiTextBlock* block = paragraph->block;
    if (!muiIsBreakUnsafe(block, line->start) && !muiIsBreakUnsafe(block, line->next))
    {
        *out = (muiLineGlyphs){block->items.data, block->itemCount, block->glyphs.data,
                               block->glyphCount, line->width};
        return true;
    }
    muiTextService* service = paragraph->service;
    muiTextLineShape shape = {&service->lineItems, 0, &service->lineGlyphs, 0};
    if (!muiShapeTextLine(service, block, &paragraph->chain, line->start, line->next, &shape))
    {
        service->failures++;
        return false;
    }
    *out = (muiLineGlyphs){service->lineItems.data, shape.itemCount, service->lineGlyphs.data,
                           shape.glyphCount, 0.0f};
    out->width = WidthBefore(paragraph, out, line->end);
    return true;
}

// Where a line starts across a content box of the width.
float muiAlignLine(const muiParagraph* paragraph, float lineWidth, float width)
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

void muiSegmentGlyphs(const muiShapedGlyph* glyphs, const muiTextItem* item, uint32_t start,
                      uint32_t end, uint32_t* firstOut, uint32_t* lastOut)
{
    // The glyphs of clusters from start up to end lie together: from the
    // first not before start to the first not before end, or for a
    // right-to-left item from the first below end to the first below
    // start.
    bool rtl = (item->level & 1) != 0;
    *firstOut = Seek(glyphs, item->glyphCount, rtl ? end : start, rtl);
    *lastOut = Seek(glyphs, item->glyphCount, rtl ? start : end, rtl);
}

bool muiReorderLine(const muiParagraph* paragraph, const muiTextLine* line, size_t* countOut)
{
    muiTextService* service = paragraph->service;
    const muiTextBlock* block = paragraph->block;
    uint32_t length = line->end - line->start;
    *countOut = 0;
    if (!muiReserve(&service->allocator, &service->runs,
                    ((size_t)length + 1u) * sizeof(muniBidiRun)))
    {
        service->failures++;
        return false;
    }
    const char* text = block->text.data;
    const uint8_t* levels = block->levels.data;
    return muniReorderBidiLine(text + line->start, levels + line->start, length,
                               paragraph->rtl ? 1 : 0, service->runs.data, length,
                               countOut) == muni_success;
}
