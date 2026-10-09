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

// The bidi levels of a paragraph's bytes from start up to end, each of
// Maul Unicode's paragraphs within them with the base direction.
static bool ResolveLevels(muiTextService* service, muiTextBlock* block, uint32_t start,
                          uint32_t end, bool rtl)
{
    if (!muiReserve(&service->allocator, &service->workspace, end - start + 1u))
    {
        return false;
    }
    const char* text = block->text.data;
    uint8_t* levels = block->levels.data;
    muniBidiDirection direction = rtl ? muni_bidiRightToLeft : muni_bidiLeftToRight;
    for (uint32_t offset = start; offset < end;)
    {
        size_t paragraph = 0;
        uint8_t level = 0;
        if (muniResolveBidi(text + offset, end - offset, direction, levels + offset,
                            service->workspace.data, &paragraph, &level) != muni_success ||
            paragraph == 0)
        {
            return false;
        }
        offset += (uint32_t)paragraph;
    }
    return true;
}

// The first script run ending past an offset.
static uint32_t ScriptAt(const muiTextBlock* block, uint32_t offset)
{
    const muiTextScript* scripts = block->scripts.data;
    uint32_t low = 0;
    uint32_t high = block->scriptCount;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        low = scripts[middle].end <= offset ? middle + 1 : low;
        high = scripts[middle].end <= offset ? high : middle;
    }
    return low;
}

// Splits a paragraph's bytes from first up to last where the level,
// script, font or run style changes; writes the items when items is not
// NULL, and returns their count.
static uint32_t SplitItems(const muiTextBlock* block, const muiFontChain* chain, uint32_t first,
                           uint32_t last, muiTextItem* items)
{
    const uint8_t* levels = block->levels.data;
    // With one font in the chain and one style, every byte is in it.
    const uint8_t* faces =
        chain->count > 1 || block->runStyleCount != 0 ? block->faces.data : nullptr;
    const muiTextScript* scripts = block->scripts.data;
    uint32_t count = 0;
    uint32_t script = ScriptAt(block, first);
    for (uint32_t start = first; start < last;)
    {
        while (scripts[script].end <= start)
        {
            script++;
        }
        uint32_t end = start + 1;
        uint32_t face = faces != nullptr ? faces[start] : 0;
        uint32_t style = muiRunOf(block, start);
        while (end < scripts[script].end && levels[end] == levels[start] &&
               (faces == nullptr || faces[end] == face) && muiRunOf(block, end) == style)
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
                .style = style,
                .scale = muiRunScale(block, style),
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

// Whether a run style's order holds a face.
static bool Holds(const uint8_t* order, uint32_t count, int64_t face)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (order[i] == face)
        {
            return true;
        }
    }
    return false;
}

// The place in the chain of the font a cluster of characters, those that
// draw, is drawn in, tried in a run style's order, given the font before
// it, or none (-1).
static uint32_t ChooseFace(const muiFontChain* chain, const uint8_t* order, uint32_t faces,
                           const uint32_t* points, uint32_t count, int64_t before)
{
    if (!Holds(order, faces, before))
    {
        before = -1;
    }
    if (count == 0)
    {
        return before >= 0 ? (uint32_t)before : order[0];
    }
    // Characters of no one script stay in the font before them.
    muniScript script = muniGetScript(points[0]);
    bool common = script == MUNI_SCRIPT_COMMON || script == MUNI_SCRIPT_INHERITED;
    if (common && before >= 0 && Covers(chain->fonts[before], points, count))
    {
        return (uint32_t)before;
    }
    for (uint32_t i = 0; i < faces; i++)
    {
        if (Covers(chain->fonts[order[i]], points, count))
        {
            return order[i];
        }
    }
    // None has them all: the first with the first character, else the
    // font before, else the first font.
    for (uint32_t i = 0; i < faces; i++)
    {
        if (Covers(chain->fonts[order[i]], points, 1))
        {
            return order[i];
        }
    }
    return common && before >= 0 ? (uint32_t)before : order[0];
}

// Writes the place in the chain of the font of each byte of a paragraph
// from first up to last, the font before its first cluster none; faces
// are kept unless the chain has one font and the block one style.
static bool ChooseFaces(const muiTextBlock* block, const muiRunChains* chains, uint32_t first,
                        uint32_t last)
{
    const muiFontChain* chain = &chains->chain;
    if (chain->count == 1 && block->runStyleCount == 0)
    {
        return true;
    }
    uint8_t* faces = (uint8_t*)block->faces.data + first;
    memset(faces, 0, last - first);
    const char* text = (const char*)block->text.data + first;
    uint32_t length = last - first;
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
        uint32_t style = muiRunOf(block, first + (uint32_t)start);
        uint32_t face = ChooseFace(chain, chains->order[style], chains->orderCount[style], points,
                                   count, before);
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
// before the piece; an item starting at begins or ending at ends starts
// or ends the text for HarfBuzz.
static bool ShapeWithin(muiTextService* service, hb_buffer_t* buffer, hb_font_t* font,
                        const muiTextBlock* block, uint32_t offset, uint32_t length,
                        uint32_t begins, uint32_t ends, const muiTextItem* range, Output* out)
{
    hb_buffer_clear_contents(buffer);
    hb_buffer_set_direction(buffer, (range->level & 1) != 0 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, (hb_script_t)range->script);
    // Default ignorables, such as bidi controls, draw nothing, so they
    // get no glyphs.
    unsigned int flags = HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES;
    flags |= range->start == begins ? HB_BUFFER_FLAG_BOT : 0u;
    flags |= range->end == ends ? HB_BUFFER_FLAG_EOT : 0u;
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
    // A damaged font's cmap or substitutions can name a glyph it does not
    // have: its missing glyph is drawn instead.
    unsigned int glyphCount = hb_face_get_glyph_count(hb_font_get_face(font));
    for (unsigned int i = 0; i < count; i++)
    {
        uint32_t cluster = infos[i].cluster + offset;
        uint32_t glyph = infos[i].codepoint < glyphCount ? infos[i].codepoint : 0;
        glyphs[i] = (muiShapedGlyph){glyph, cluster, positions[i].x_advance, positions[i].x_offset,
                                     positions[i].y_offset};
        if (out->unsafe != nullptr &&
            (hb_glyph_info_get_glyph_flags(&infos[i]) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0)
        {
            out->unsafe[cluster] = 1;
        }
    }
    *out->count += count;
    return true;
}

// ShapeWithin for a line of the whole text.
static bool ShapeRange(muiTextService* service, hb_buffer_t* buffer, hb_font_t* font,
                       const muiTextBlock* block, uint32_t offset, uint32_t length,
                       const muiTextItem* range, Output* out)
{
    return ShapeWithin(service, buffer, font, block, offset, length, 0, block->length, range, out);
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

// Sums of a paragraph's advances, in ems, and of its cluster starts,
// before each of its bytes from first up to last and at last, from 0 at
// first. A cluster's advance counts at its first byte.
static void SumAdvances(muiTextBlock* block, uint32_t first, uint32_t last,
                        const muiTextItem* items, uint32_t itemCount, const muiShapedGlyph* glyphs)
{
    double* advances = (double*)block->advances.data + first;
    uint32_t* clusters = (uint32_t*)block->clusters.data + first;
    uint32_t length = last - first;
    memset(advances, 0, (length + 1u) * sizeof(double));
    memset(clusters, 0, (length + 1u) * sizeof(uint32_t));
    for (uint32_t k = 0; k < itemCount; k++)
    {
        const muiShapedGlyph* own = glyphs + items[k].firstGlyph;
        // In ems of the node's size: a run's own size over it.
        double perUnit = (double)items[k].scale / (double)items[k].units;
        for (uint32_t i = 0; i < items[k].glyphCount; i++)
        {
            advances[own[i].cluster - first + 1] += (double)own[i].advance * perUnit;
            clusters[own[i].cluster - first + 1] = 1;
        }
    }
    for (uint32_t i = 1; i <= length; i++)
    {
        advances[i] += advances[i - 1];
        clusters[i] += clusters[i - 1];
    }
}

// The HarfBuzz font of an item's face in a chain.
static hb_font_t* ShaperOf(const muiFontChain* chain, const muiTextItem* item)
{
    return muiShapingFontOf(chain->fonts[item->face], chain->keys[item->face]);
}

// What shaping a region of whole paragraphs makes: its items and glyphs,
// apart from the block's; the per-byte tables go into the block's.
typedef struct Region
{
    muiBuffer items;
    uint32_t itemCount;
    muiBuffer glyphs;
    uint32_t glyphCount;
} Region;

static void FreeRegion(const muiAllocator* allocator, Region* region)
{
    muiFreeBuffer(allocator, &region->items);
    muiFreeBuffer(allocator, &region->glyphs);
}

// Shapes a paragraph from first up to last on its own: its levels,
// fonts, items and glyphs, HarfBuzz taking it as the whole text, and its
// sums, appended to a region.
static bool ShapeParagraph(muiTextService* service, muiTextBlock* block, const muiRunChains* chains,
                           bool rtl, uint32_t first, uint32_t last, hb_buffer_t* buffer,
                           Region* region)
{
    const muiFontChain* chain = &chains->chain;
    if (!ResolveLevels(service, block, first, last, rtl) ||
        !ChooseFaces(block, chains, first, last))
    {
        return false;
    }
    memset((uint8_t*)block->unsafe.data + first, 0, last - first);
    uint32_t count = SplitItems(block, chain, first, last, nullptr);
    uint32_t kept = region->itemCount;
    if (!muiReserveKeeping(&service->allocator, &region->items,
                           ((size_t)kept + count + 1u) * sizeof(muiTextItem),
                           (size_t)kept * sizeof(muiTextItem)))
    {
        return false;
    }
    muiTextItem* items = (muiTextItem*)region->items.data + kept;
    (void)SplitItems(block, chain, first, last, items);
    region->itemCount += count;
    bool shaped = true;
    Output out = {&region->glyphs, &region->glyphCount, block->unsafe.data};
    for (uint32_t i = 0; shaped && i < count; i++)
    {
        items[i].firstGlyph = region->glyphCount;
        hb_font_t* shaper = ShaperOf(chain, &items[i]);
        shaped = shaper != nullptr && ShapeWithin(service, buffer, shaper, block, first,
                                                  last - first, first, last, &items[i], &out);
        items[i].glyphCount = region->glyphCount - items[i].firstGlyph;
    }
    if (shaped)
    {
        SumAdvances(block, first, last, items, count, region->glyphs.data);
    }
    return shaped;
}

// Shapes the paragraphs from start up to end into a region; the block's
// per-byte tables hold room for its whole text.
static bool ShapeRegion(muiTextService* service, muiTextBlock* block, const muiRunChains* chains,
                        bool rtl, uint32_t start, uint32_t end, Region* region)
{
    hb_buffer_t* buffer = MakeBuffer(service);
    bool shaped = buffer != nullptr;
    const char* text = block->text.data;
    // An empty text's sums are its start's.
    ((double*)block->advances.data)[start] = 0.0;
    ((uint32_t*)block->clusters.data)[start] = 0;
    for (uint32_t at = start; shaped && at < end;)
    {
        uint32_t next = muiParagraphEnd(text, end, at);
        shaped = ShapeParagraph(service, block, chains, rtl, at, next, buffer, region);
        at = next;
    }
    hb_buffer_destroy(buffer);
    // The sums at the region's end start the paragraph after it.
    if (shaped && end < block->length)
    {
        ((double*)block->advances.data)[end] = 0.0;
        ((uint32_t*)block->clusters.data)[end] = 0;
    }
    return shaped;
}

// Room in the block's per-byte tables for its text, keeping kept entries.
static bool ReserveTables(muiTextService* service, muiTextBlock* block, const muiFontChain* chain,
                          size_t kept)
{
    const muiAllocator* allocator = &service->allocator;
    size_t entries = (size_t)block->length + 1u;
    bool faces = chain->count > 1 || block->runStyleCount != 0;
    return muiReserveKeeping(allocator, &block->levels, entries, kept) &&
           (!faces || muiReserveKeeping(allocator, &block->faces, entries, kept)) &&
           muiReserveKeeping(allocator, &block->unsafe, entries, kept) &&
           muiReserveKeeping(allocator, &block->advances, entries * sizeof(double),
                             kept * sizeof(double)) &&
           muiReserveKeeping(allocator, &block->clusters, entries * sizeof(uint32_t),
                             kept * sizeof(uint32_t));
}

// Shapes the block's whole text.
static bool Shape(muiTextService* service, muiTextBlock* block, const muiRunChains* chains,
                  bool rtl)
{
    block->shapings++;
    block->glyphCount = 0;
    block->itemCount = 0;
    Region region = {0};
    bool shaped = ReserveTables(service, block, &chains->chain, 0) &&
                  ShapeRegion(service, block, chains, rtl, 0, block->length, &region);
    if (shaped)
    {
        muiFreeBuffer(&service->allocator, &block->items);
        muiFreeBuffer(&service->allocator, &block->glyphs);
        block->items = region.items;
        block->itemCount = region.itemCount;
        block->glyphs = region.glyphs;
        block->glyphCount = region.glyphCount;
        region = (Region){0};
    }
    FreeRegion(&service->allocator, &region);
    return shaped;
}

// Moves a table's entries from from on (count of them, each of a size)
// to to.
static void MoveEntries(muiBuffer* buffer, size_t size, uint32_t from, uint32_t to, size_t count)
{
    if (buffer->data != nullptr && count != 0)
    {
        memmove((char*)buffer->data + (size_t)to * size, (char*)buffer->data + (size_t)from * size,
                count * size);
    }
}

// The block's items and glyphs with the region's in place of those from
// start up to oldEnd of the text it was shaped for, those after moved by
// delta bytes.
static bool SpliceItems(muiTextService* service, muiTextBlock* block, uint32_t start,
                        uint32_t oldEnd, int64_t delta, const Region* region)
{
    muiTextItem* items = block->items.data;
    uint32_t before = 0;
    while (before < block->itemCount && items[before].end <= start)
    {
        before++;
    }
    uint32_t after = before;
    while (after < block->itemCount && items[after].start < oldEnd)
    {
        after++;
    }
    uint32_t glyphsBefore =
        before > 0 ? items[before - 1].firstGlyph + items[before - 1].glyphCount : 0;
    uint32_t glyphsAfter = after < block->itemCount ? items[after].firstGlyph : block->glyphCount;
    uint32_t tailItems = block->itemCount - after;
    uint32_t tailGlyphs = block->glyphCount - glyphsAfter;
    uint32_t itemCount = before + region->itemCount + tailItems;
    uint32_t glyphCount = glyphsBefore + region->glyphCount + tailGlyphs;
    const muiAllocator* allocator = &service->allocator;
    if (!muiReserveKeeping(allocator, &block->items, ((size_t)itemCount + 1u) * sizeof(muiTextItem),
                           (size_t)block->itemCount * sizeof(muiTextItem)) ||
        !muiReserveKeeping(allocator, &block->glyphs,
                           ((size_t)glyphCount + 1u) * sizeof(muiShapedGlyph),
                           (size_t)block->glyphCount * sizeof(muiShapedGlyph)))
    {
        return false;
    }
    MoveEntries(&block->items, sizeof(muiTextItem), after, before + region->itemCount, tailItems);
    MoveEntries(&block->glyphs, sizeof(muiShapedGlyph), glyphsAfter,
                glyphsBefore + region->glyphCount, tailGlyphs);
    items = block->items.data;
    muiShapedGlyph* glyphs = block->glyphs.data;
    const muiTextItem* own = region->items.data;
    for (uint32_t i = 0; i < region->itemCount; i++)
    {
        items[before + i] = own[i];
        items[before + i].firstGlyph += glyphsBefore;
    }
    if (region->glyphCount != 0)
    {
        memcpy(glyphs + glyphsBefore, region->glyphs.data,
               region->glyphCount * sizeof(muiShapedGlyph));
    }
    for (uint32_t i = before + region->itemCount; i < itemCount; i++)
    {
        items[i].start = (uint32_t)((int64_t)items[i].start + delta);
        items[i].end = (uint32_t)((int64_t)items[i].end + delta);
        items[i].firstGlyph = items[i].firstGlyph - glyphsAfter + glyphsBefore + region->glyphCount;
    }
    for (uint32_t i = glyphsBefore + region->glyphCount; i < glyphCount; i++)
    {
        glyphs[i].cluster = (uint32_t)((int64_t)glyphs[i].cluster + delta);
    }
    block->itemCount = itemCount;
    block->glyphCount = glyphCount;
    return true;
}

// Shapes again only the block's stale paragraphs: the per-byte tables'
// entries after them move by the change in length, and the items and
// glyphs of the paragraphs between are replaced, as shaping the whole
// text would make them, paragraph by paragraph.
static bool Reshape(muiTextService* service, muiTextBlock* block, const muiRunChains* chains,
                    bool rtl)
{
    uint32_t start = block->stale.start;
    uint32_t end = block->stale.end;
    int64_t delta = (int64_t)block->length - (int64_t)block->shapedLength;
    uint32_t oldEnd = (uint32_t)((int64_t)end - delta);
    size_t tail = (size_t)block->shapedLength - oldEnd + 1u;
    bool faces = chains->chain.count > 1 || block->runStyleCount != 0;
    if (!ReserveTables(service, block, &chains->chain, (size_t)block->shapedLength + 1u))
    {
        return false;
    }
    MoveEntries(&block->levels, 1, oldEnd, end, tail);
    if (faces)
    {
        MoveEntries(&block->faces, 1, oldEnd, end, tail);
    }
    MoveEntries(&block->unsafe, 1, oldEnd, end, tail);
    MoveEntries(&block->advances, sizeof(double), oldEnd, end, tail);
    MoveEntries(&block->clusters, sizeof(uint32_t), oldEnd, end, tail);
    Region region = {0};
    bool shaped = ShapeRegion(service, block, chains, rtl, start, end, &region) &&
                  SpliceItems(service, block, start, oldEnd, delta, &region);
    FreeRegion(&service->allocator, &region);
    return shaped;
}

bool muiShapeTextBlock(muiTextService* service, muiTextBlock* block, const muiRunChains* chains,
                       bool rtl)
{
    bool same =
        block->shaped && block->shapedChain == chains->chain.identity && block->shapedRtl == rtl;
    if (same && !block->stale.on)
    {
        return true;
    }
    block->shaped = same && block->runStyleCount == 0 ? Reshape(service, block, chains, rtl)
                                                      : Shape(service, block, chains, rtl);
    block->shapedChain = chains->chain.identity;
    block->shapedRtl = rtl;
    block->shapedLength = block->length;
    block->stale.on = false;
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
