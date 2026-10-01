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

static bool ShapeItem(muiTextService* service, muiTextBlock* block, hb_buffer_t* buffer,
                      const muiFont* font, muiTextItem* item)
{
    hb_buffer_clear_contents(buffer);
    hb_buffer_set_direction(buffer, (item->level & 1) != 0 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, (hb_script_t)item->script);
    // Default ignorables, such as bidi controls, draw nothing, so they
    // get no glyphs.
    unsigned int flags = HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES;
    flags |= item->start == 0 ? HB_BUFFER_FLAG_BOT : 0u;
    flags |= item->end == block->length ? HB_BUFFER_FLAG_EOT : 0u;
    hb_buffer_set_flags(buffer, (hb_buffer_flags_t)flags);
    hb_buffer_add_utf8(buffer, block->text.data, (int)block->length, item->start,
                       (int)(item->end - item->start));
    hb_shape(font->shapingFont, buffer, nullptr, 0);
    if (!hb_buffer_allocation_successful(buffer))
    {
        return false;
    }
    unsigned int count = hb_buffer_get_length(buffer);
    if (!muiReserveKeeping(&service->allocator, &block->glyphs,
                           ((size_t)block->glyphCount + count) * sizeof(muiShapedGlyph),
                           (size_t)block->glyphCount * sizeof(muiShapedGlyph)))
    {
        return false;
    }
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, nullptr);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, nullptr);
    muiShapedGlyph* glyphs = (muiShapedGlyph*)block->glyphs.data + block->glyphCount;
    for (unsigned int i = 0; i < count; i++)
    {
        glyphs[i] = (muiShapedGlyph){infos[i].codepoint, infos[i].cluster, positions[i].x_advance,
                                     positions[i].x_offset, positions[i].y_offset};
    }
    item->firstGlyph = block->glyphCount;
    item->glyphCount = count;
    block->glyphCount += count;
    return true;
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
    if (!ResolveLevels(service, block, rtl))
    {
        return false;
    }
    uint32_t count = SplitItems(block, nullptr);
    if (!muiReserve(&service->allocator, &block->items, (count + 1u) * sizeof(muiTextItem)))
    {
        return false;
    }
    muiTextItem* items = block->items.data;
    (void)SplitItems(block, items);
    block->itemCount = count;
    hb_buffer_t* buffer = hb_buffer_create();
    bool shaped = hb_buffer_allocation_successful(buffer);
    if (shaped)
    {
        hb_buffer_set_unicode_funcs(buffer, service->unicode);
    }
    for (uint32_t i = 0; shaped && i < count; i++)
    {
        shaped = ShapeItem(service, block, buffer, font, &items[i]);
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
