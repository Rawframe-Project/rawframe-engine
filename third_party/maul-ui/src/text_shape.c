// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shaping a text block (record mui-0006). Buffers carry no language, so
// the result does not depend on the process's locale.

#include "text_shape.h"

#include "font_instance.h"

#include "maul-unicode/bidi.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"
#include "maul-unicode/segment.h"

#include <hb.h>
#include <string.h>

enum
{
    // The characters of a cluster whose fonts are looked at.
    MAX_CLUSTER_POINTS = 32
};

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

// Splits the text where its level, script or font changes; writes the
// items when items is not NULL, and returns their count.
static uint32_t SplitItems(const muiTextBlock* block, const muiFontChain* chain, muiTextItem* items)
{
    const uint8_t* levels = block->levels.data;
    // With one font in the chain, every byte is in it.
    const uint8_t* faces = chain->count > 1 ? block->faces.data : nullptr;
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
        uint32_t face = faces != nullptr ? faces[start] : 0;
        while (end < scripts[script].end && levels[end] == levels[start] &&
               (faces == nullptr || faces[end] == face))
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
                .face = face,
                .units = chain->fonts[face]->metrics.unitsPerEm,
            };
        }
        count++;
        start = end;
    }
    return count;
}

// Whether a font has a glyph for every character of a cluster that
// draws: default ignorables and controls are passed over.
static bool Covers(const muiFont* font, const uint32_t* points, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        hb_codepoint_t glyph = 0;
        if (!hb_font_get_nominal_glyph(font->shapingFont, points[i], &glyph))
        {
            return false;
        }
    }
    return true;
}

// The place in the chain of the font a cluster of characters, those that
// draw, is drawn in, given the font before it, or none (-1).
static uint32_t ChooseFace(const muiFontChain* chain, const uint32_t* points, uint32_t count,
                           int64_t before)
{
    if (count == 0)
    {
        return before >= 0 ? (uint32_t)before : 0;
    }
    // Characters of no one script stay in the font before them.
    muniScript script = muniGetScript(points[0]);
    bool common = script == MUNI_SCRIPT_COMMON || script == MUNI_SCRIPT_INHERITED;
    if (common && before >= 0 && Covers(chain->fonts[before], points, count))
    {
        return (uint32_t)before;
    }
    for (uint32_t face = 0; face < chain->count; face++)
    {
        if (Covers(chain->fonts[face], points, count))
        {
            return face;
        }
    }
    // None has them all: the first with the first character, else the
    // font before, else the first font.
    for (uint32_t face = 0; face < chain->count; face++)
    {
        if (Covers(chain->fonts[face], points, 1))
        {
            return face;
        }
    }
    return common && before >= 0 ? (uint32_t)before : 0;
}

// Writes the place in the chain of each byte's font, unless the chain
// has one font.
static bool ChooseFaces(muiTextService* service, muiTextBlock* block, const muiFontChain* chain)
{
    uint32_t length = block->length;
    if (chain->count == 1)
    {
        return true;
    }
    if (!muiReserve(&service->allocator, &block->faces, length + 1u))
    {
        return false;
    }
    uint8_t* faces = block->faces.data;
    memset(faces, 0, length + 1u);
    const char* text = block->text.data;
    muniSegmentIterator graphemes;
    if (muniInitGraphemeIterator(&graphemes, text, length, false) != muni_success)
    {
        return false;
    }
    int64_t before = -1;
    size_t start = 0;
    size_t end = 0;
    while (muniNextSegmentBreak(&graphemes, &end) == muni_success)
    {
        if (end == 0)
        {
            continue;
        }
        // The cluster's characters that draw, as many as fit; a cluster
        // longer than that is judged by its start.
        uint32_t points[MAX_CLUSTER_POINTS];
        uint32_t count = 0;
        for (size_t at = start; at < end && count < MAX_CLUSTER_POINTS;)
        {
            uint32_t point = 0;
            size_t size = 1;
            (void)muniDecodeUtf8(text + at, end - at, &point, &size);
            if (!muniIsDefaultIgnorable(point) && muniGetGeneralCategory(point) != muni_gcCc)
            {
                points[count++] = point;
            }
            at += size;
        }
        uint32_t face = ChooseFace(chain, points, count, before);
        memset(faces + start, (int)face, end - start);
        before = face;
        start = end;
    }
    return true;
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
static bool ShapeRange(muiTextService* service, hb_buffer_t* buffer, hb_font_t* font,
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
    hb_shape(font, buffer, nullptr, 0);
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

// Sums of advances, in ems, and of cluster starts before each byte. A
// cluster's advance counts at its first byte.
static bool SumAdvances(muiTextService* service, muiTextBlock* block)
{
    uint32_t length = block->length;
    if (!muiReserve(&service->allocator, &block->advances, (length + 1u) * sizeof(double)) ||
        !muiReserve(&service->allocator, &block->clusters, (length + 1u) * sizeof(uint32_t)))
    {
        return false;
    }
    double* advances = block->advances.data;
    uint32_t* clusters = block->clusters.data;
    memset(advances, 0, (length + 1u) * sizeof(double));
    memset(clusters, 0, (length + 1u) * sizeof(uint32_t));
    const muiShapedGlyph* glyphs = block->glyphs.data;
    const muiTextItem* items = block->items.data;
    for (uint32_t k = 0; k < block->itemCount; k++)
    {
        const muiShapedGlyph* own = glyphs + items[k].firstGlyph;
        double perUnit = 1.0 / (double)items[k].units;
        for (uint32_t i = 0; i < items[k].glyphCount; i++)
        {
            advances[own[i].cluster + 1] += (double)own[i].advance * perUnit;
            clusters[own[i].cluster + 1] = 1;
        }
    }
    for (uint32_t i = 1; i <= length; i++)
    {
        advances[i] += advances[i - 1];
        clusters[i] += clusters[i - 1];
    }
    return true;
}

// The HarfBuzz font of an item's face in a chain.
static hb_font_t* ShaperOf(const muiFontChain* chain, const muiTextItem* item)
{
    return muiShapingFontOf(chain->fonts[item->face], chain->keys[item->face]);
}

static bool Shape(muiTextService* service, muiTextBlock* block, const muiFontChain* chain, bool rtl)
{
    block->glyphCount = 0;
    block->itemCount = 0;
    uint32_t length = block->length;
    if (!ResolveLevels(service, block, rtl) || !ChooseFaces(service, block, chain) ||
        !muiReserve(&service->allocator, &block->unsafe, length + 1u))
    {
        return false;
    }
    memset(block->unsafe.data, 0, length + 1u);
    uint32_t count = SplitItems(block, chain, nullptr);
    if (!muiReserve(&service->allocator, &block->items, (count + 1u) * sizeof(muiTextItem)))
    {
        return false;
    }
    muiTextItem* items = block->items.data;
    (void)SplitItems(block, chain, items);
    block->itemCount = count;
    hb_buffer_t* buffer = MakeBuffer(service);
    bool shaped = buffer != nullptr;
    Output out = {&block->glyphs, &block->glyphCount, block->unsafe.data};
    for (uint32_t i = 0; shaped && i < count; i++)
    {
        items[i].firstGlyph = block->glyphCount;
        hb_font_t* shaper = ShaperOf(chain, &items[i]);
        shaped = shaper != nullptr &&
                 ShapeRange(service, buffer, shaper, block, 0, length, &items[i], &out);
        items[i].glyphCount = block->glyphCount - items[i].firstGlyph;
    }
    hb_buffer_destroy(buffer);
    return shaped && SumAdvances(service, block);
}

bool muiShapeTextBlock(muiTextService* service, muiTextBlock* block, const muiFontChain* chain,
                       bool rtl)
{
    if (block->shaped && block->shapedChain == chain->identity && block->shapedRtl == rtl)
    {
        return true;
    }
    block->shaped = Shape(service, block, chain, rtl);
    block->shapedChain = chain->identity;
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

bool muiShapeTextLine(muiTextService* service, const muiTextBlock* block, const muiFontChain* chain,
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
        hb_font_t* shaper = ShaperOf(chain, piece);
        shaped = shaper != nullptr &&
                 ShapeRange(service, buffer, shaper, block, start, end - start, piece, &glyphs);
        piece->glyphCount = out->glyphCount - piece->firstGlyph;
    }
    hb_buffer_destroy(buffer);
    return shaped;
}
