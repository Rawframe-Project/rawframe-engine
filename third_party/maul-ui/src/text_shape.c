// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shaping a text block (record mui-0006). Buffers carry no language, so
// the result does not depend on the process's locale.

#include "text_shape.h"

#include "maul-unicode/bidi.h"

#include <hb.h>
#include <string.h>

// The bidi levels of every paragraph of the text, each with the base
// direction.
static bool ResolveLevels(muiTextService* service, muiTextBlock* block, bool rtl)
{
    const muiAllocator* allocator = &service->allocator;
    uint32_t length = block->length;
    if (!muiReserve(allocator, &block->levels, length + 1u) ||
        !muiReserve(allocator, &service->workspace, length + 1u))
    {
        return false;
    }
    const char* text = block->text.data;
    uint8_t* levels = block->levels.data;
    muniBidiDirection direction = rtl ? muni_bidiRightToLeft : muni_bidiLeftToRight;
    for (uint32_t offset = 0; offset < length;)
    {
        size_t paragraph = 0;
        uint8_t level = 0;
        if (muniResolveBidi(text + offset, length - offset, direction, levels + offset,
                            service->workspace.data, &paragraph, &level) != muni_success ||
            paragraph == 0)
        {
            return false;
        }
        offset += (uint32_t)paragraph;
    }
    return true;
}

// Splits the text where its level or script changes; writes the items
// when items is not NULL, and returns their count.
static uint32_t SplitItems(const muiTextBlock* block, muiTextItem* items)
{
    const uint8_t* levels = block->levels.data;
    const muiTextScript* scripts = block->scripts.data;
    uint32_t count = 0;
    uint32_t script = 0;
    for (uint32_t start = 0; start < block->length;)
    {
        while (scripts[script].end <= start)
        {
            script++;
        }
        uint32_t end = start + 1;
        while (end < scripts[script].end && levels[end] == levels[start])
        {
            end++;
        }
        if (items != nullptr)
        {
            items[count] = (muiTextItem){
                .start = start,
                .end = end,
                .level = levels[start],
                .script = scripts[script].script,
            };
        }
        count++;
        start = end;
    }
    return count;
}

// Glyphs being appended to: the buffer, how many it holds, and the
// per-byte unsafe-to-break marks to set, if any.
typedef struct Output
{
    muiBuffer* glyphs;
    uint32_t* count;
    uint8_t* unsafe;
} Output;

// Shapes bytes from to up to to of a piece of text as one item of the
// level and script, the piece being the context, and appends its glyphs
// with clusters as offsets in the whole text, which starts offset bytes
// before the piece.
static bool ShapeRange(muiTextService* service, hb_buffer_t* buffer, const muiFont* font,
                       const muiTextBlock* block, uint32_t offset, uint32_t length,
                       const muiTextItem* range, Output* out)
{
    hb_buffer_clear_contents(buffer);
    hb_buffer_set_direction(buffer, (range->level & 1) != 0 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, (hb_script_t)range->script);
    // Default ignorables, such as bidi controls, draw nothing, so they
    // get no glyphs.
    unsigned int flags = HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES;
    flags |= range->start == 0 ? HB_BUFFER_FLAG_BOT : 0u;
    flags |= range->end == block->length ? HB_BUFFER_FLAG_EOT : 0u;
    hb_buffer_set_flags(buffer, (hb_buffer_flags_t)flags);
    const char* text = block->text.data;
    hb_buffer_add_utf8(buffer, text + offset, (int)length, range->start - offset,
                       (int)(range->end - range->start));
    hb_shape(font->shapingFont, buffer, nullptr, 0);
    if (!hb_buffer_allocation_successful(buffer))
    {
        return false;
    }
    unsigned int count = hb_buffer_get_length(buffer);
    if (!muiReserveKeeping(&service->allocator, out->glyphs,
                           ((size_t)*out->count + count) * sizeof(muiShapedGlyph),
                           (size_t)*out->count * sizeof(muiShapedGlyph)))
    {
        return false;
    }
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, nullptr);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, nullptr);
    muiShapedGlyph* glyphs = (muiShapedGlyph*)out->glyphs->data + *out->count;
    for (unsigned int i = 0; i < count; i++)
    {
        uint32_t cluster = infos[i].cluster + offset;
        glyphs[i] = (muiShapedGlyph){infos[i].codepoint, cluster, positions[i].x_advance,
                                     positions[i].x_offset, positions[i].y_offset};
        if (out->unsafe != nullptr &&
            (hb_glyph_info_get_glyph_flags(&infos[i]) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0)
        {
            out->unsafe[cluster] = 1;
        }
    }
    *out->count += count;
    return true;
}

static hb_buffer_t* MakeBuffer(const muiTextService* service)
{
    hb_buffer_t* buffer = hb_buffer_create();
    if (!hb_buffer_allocation_successful(buffer))
    {
        hb_buffer_destroy(buffer);
        return nullptr;
    }
    hb_buffer_set_unicode_funcs(buffer, service->unicode);
    return buffer;
}

// Sums of advances and of cluster starts before each byte. A cluster's
// advance counts at its first byte.
static bool SumAdvances(muiTextService* service, muiTextBlock* block)
{
    uint32_t length = block->length;
    if (!muiReserve(&service->allocator, &block->advances, (length + 1u) * sizeof(int64_t)) ||
        !muiReserve(&service->allocator, &block->clusters, (length + 1u) * sizeof(uint32_t)))
    {
        return false;
    }
    int64_t* advances = block->advances.data;
    uint32_t* clusters = block->clusters.data;
    memset(advances, 0, (length + 1u) * sizeof(int64_t));
    memset(clusters, 0, (length + 1u) * sizeof(uint32_t));
    const muiShapedGlyph* glyphs = block->glyphs.data;
    for (uint32_t i = 0; i < block->glyphCount; i++)
    {
        advances[glyphs[i].cluster + 1] += glyphs[i].advance;
        clusters[glyphs[i].cluster + 1] = 1;
    }
    for (uint32_t i = 1; i <= length; i++)
    {
        advances[i] += advances[i - 1];
        clusters[i] += clusters[i - 1];
    }
    return true;
}

static bool Shape(muiTextService* service, muiTextBlock* block, const muiFont* font, bool rtl)
{
    block->glyphCount = 0;
    block->itemCount = 0;
    uint32_t length = block->length;
    if (!ResolveLevels(service, block, rtl) ||
        !muiReserve(&service->allocator, &block->unsafe, length + 1u))
    {
        return false;
    }
    memset(block->unsafe.data, 0, length + 1u);
    uint32_t count = SplitItems(block, nullptr);
    if (!muiReserve(&service->allocator, &block->items, (count + 1u) * sizeof(muiTextItem)))
    {
        return false;
    }
    muiTextItem* items = block->items.data;
    (void)SplitItems(block, items);
    block->itemCount = count;
    hb_buffer_t* buffer = MakeBuffer(service);
    bool shaped = buffer != nullptr;
    Output out = {&block->glyphs, &block->glyphCount, block->unsafe.data};
    for (uint32_t i = 0; shaped && i < count; i++)
    {
        items[i].firstGlyph = block->glyphCount;
        shaped = ShapeRange(service, buffer, font, block, 0, length, &items[i], &out);
        items[i].glyphCount = block->glyphCount - items[i].firstGlyph;
    }
    hb_buffer_destroy(buffer);
    return shaped && SumAdvances(service, block);
}

bool muiShapeTextBlock(muiTextService* service, muiTextBlock* block, const muiFont* font,
                       uint64_t fontKey, bool rtl)
{
    if (block->shaped && block->shapedFont == fontKey && block->shapedRtl == rtl)
    {
        return true;
    }
    block->shaped = Shape(service, block, font, rtl);
    block->shapedFont = fontKey;
    block->shapedRtl = rtl;
    return block->shaped;
}

bool muiIsBreakUnsafe(const muiTextBlock* block, uint32_t offset)
{
    if (offset == 0 || offset >= block->length)
    {
        return false;
    }
    const uint32_t* clusters = block->clusters.data;
    const uint8_t* unsafe = block->unsafe.data;
    return clusters[offset + 1] == clusters[offset] || unsafe[offset] != 0;
}

bool muiShapeTextLine(muiTextService* service, const muiTextBlock* block, const muiFont* font,
                      uint32_t start, uint32_t end, muiTextLineShape* out)
{
    out->glyphCount = 0;
    out->itemCount = 0;
    const muiTextItem* items = block->items.data;
    uint32_t pieces = 0;
    for (uint32_t i = 0; i < block->itemCount; i++)
    {
        pieces += items[i].end > start && items[i].start < end ? 1u : 0u;
    }
    if (!muiReserve(&service->allocator, out->items, (pieces + 1u) * sizeof(muiTextItem)))
    {
        return false;
    }
    hb_buffer_t* buffer = MakeBuffer(service);
    bool shaped = buffer != nullptr;
    Output glyphs = {out->glyphs, &out->glyphCount, nullptr};
    muiTextItem* own = out->items->data;
    for (uint32_t i = 0; shaped && i < block->itemCount; i++)
    {
        if (items[i].end <= start || items[i].start >= end)
        {
            continue;
        }
        muiTextItem* piece = &own[out->itemCount++];
        *piece = items[i];
        piece->start = items[i].start > start ? items[i].start : start;
        piece->end = items[i].end < end ? items[i].end : end;
        piece->firstGlyph = out->glyphCount;
        shaped = ShapeRange(service, buffer, font, block, start, end - start, piece, &glyphs);
        piece->glyphCount = out->glyphCount - piece->firstGlyph;
    }
    hb_buffer_destroy(buffer);
    return shaped;
}
