// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Run styles (record mui-0006).

#include "text_runs.h"

#include "maul-ui/style.h"
#include "maul-ui/text_block.h"

#include <string.h>

// What of a span makes run styles: what shaping reads, and the shift.
#define RUN_PROPERTIES                                                                             \
    (MUI_PROPERTY_BIT(mui_propertyFont) | MUI_PROPERTY_BIT(mui_propertyFontSize) |                 \
     MUI_PROPERTY_BIT(mui_propertyFontWeight) | MUI_PROPERTY_BIT(mui_propertyFontSlant) |          \
     MUI_PROPERTY_BIT(mui_propertyTextBaselineShift))

// FNV-1a over bytes.
static uint64_t Mix(uint64_t hash, const void* data, size_t size)
{
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; i++)
    {
        hash = (hash ^ bytes[i]) * 0x100000001B3u;
    }
    return hash;
}

// What a block's run styles are made for: the node's shaping values and
// the spans that shape, with what they set; 0 when no span shapes.
static uint64_t KeyOf(const muiTextBlock* block, const muiComputedTextStyle* style)
{
    const muiTextSpan* spans = block->spans.data;
    uint64_t key = 0xCBF29CE484222325u;
    bool shapes = false;
    for (uint32_t i = 0; i < block->spanCount; i++)
    {
        const muiTextSpan* span = &spans[i];
        muiPropertyMask mask = span->mask & RUN_PROPERTIES;
        if (mask == 0)
        {
            continue;
        }
        shapes = true;
        const uint32_t place[2] = {span->start, span->length};
        key = Mix(key, place, sizeof place);
        key = Mix(key, &mask, sizeof mask);
        key = Mix(key, &span->style.font, sizeof span->style.font);
        key = Mix(key, &span->style.weight, sizeof span->style.weight);
        key = Mix(key, &span->style.slant, sizeof span->style.slant);
        const float size[4] = {span->style.size.scale, span->style.size.offset,
                               span->style.baselineShift.scale, span->style.baselineShift.offset};
        key = Mix(key, size, sizeof size);
    }
    if (!shapes)
    {
        return 0;
    }
    key = Mix(key, &style->font, sizeof style->font);
    key = Mix(key, &style->weight, sizeof style->weight);
    key = Mix(key, &style->slant, sizeof style->slant);
    key = Mix(key, &style->size, sizeof style->size);
    return key | 1u;
}

// A run style under a span: what the span sets, its size against the
// node's as a child's text is against its parent's.
static muiRunStyle Apply(muiRunStyle under, const muiTextSpan* span, float nodeSize)
{
    if ((span->mask & MUI_PROPERTY_BIT(mui_propertyFont)) != 0)
    {
        under.font = span->style.font;
    }
    if ((span->mask & MUI_PROPERTY_BIT(mui_propertyFontWeight)) != 0)
    {
        under.weight = span->style.weight;
    }
    if ((span->mask & MUI_PROPERTY_BIT(mui_propertyFontSlant)) != 0)
    {
        under.slant = span->style.slant;
    }
    if ((span->mask & MUI_PROPERTY_BIT(mui_propertyFontSize)) != 0)
    {
        under.size = span->style.size.scale * nodeSize + span->style.size.offset;
        // A node of size 0 draws nothing, its spans included.
        under.scale = nodeSize > 0.0f ? under.size / nodeSize : 1.0f;
    }
    if ((span->mask & MUI_PROPERTY_BIT(mui_propertyTextBaselineShift)) != 0)
    {
        under.shift = span->style.baselineShift.scale * nodeSize + span->style.baselineShift.offset;
    }
    return under;
}

static bool IsSameRun(const muiRunStyle* a, const muiRunStyle* b)
{
    return a->font == b->font && a->weight == b->weight && a->slant == b->slant &&
           a->size == b->size && a->shift == b->shift;
}

// The place of a run style in the table, added when new; fallback when
// the table is full.
static uint32_t Find(muiRunStyle* styles, uint32_t* count, const muiRunStyle* want,
                     uint32_t fallback)
{
    for (uint32_t i = 0; i < *count; i++)
    {
        if (IsSameRun(&styles[i], want))
        {
            return i;
        }
    }
    if (*count == MUI_MAX_RUN_STYLES)
    {
        return fallback;
    }
    styles[*count] = *want;
    return (*count)++;
}

bool muiPrepareRuns(muiTextService* service, muiTextBlock* block, const muiComputedTextStyle* style)
{
    uint64_t key = KeyOf(block, style);
    if (key == 0)
    {
        block->runStyleCount = 0;
        return true;
    }
    if (key == block->runKey && block->runStyleCount != 0)
    {
        return true;
    }
    uint32_t length = block->length;
    if (!muiReserve(&service->allocator, &block->runStyles,
                    MUI_MAX_RUN_STYLES * sizeof(muiRunStyle)) ||
        !muiReserve(&service->allocator, &block->runs, length + 1u))
    {
        block->runStyleCount = 0;
        return false;
    }
    muiRunStyle* styles = block->runStyles.data;
    uint8_t* runs = block->runs.data;
    styles[0] = (muiRunStyle){style->font, style->weight, style->size, 1.0f, 0.0f, style->slant};
    uint32_t count = 1;
    memset(runs, 0, length + 1u);
    const muiTextSpan* spans = block->spans.data;
    for (uint32_t i = 0; i < block->spanCount; i++)
    {
        const muiTextSpan* span = &spans[i];
        if ((span->mask & RUN_PROPERTIES) == 0)
        {
            continue;
        }
        // Bytes in a span mostly share the style under it: the last one
        // found stands until that changes.
        uint32_t from = MUI_MAX_RUN_STYLES;
        uint32_t to = 0;
        for (uint32_t at = span->start; at < span->start + span->length; at++)
        {
            if (runs[at] != from)
            {
                from = runs[at];
                const muiRunStyle want = Apply(styles[from], span, style->size);
                to = Find(styles, &count, &want, from);
            }
            runs[at] = (uint8_t)to;
        }
    }
    block->runStyleCount = count;
    block->runKey = key;
    return true;
}

// Adds a style's chain to a paragraph's, sharing the faces it has, and
// writes its order of trying them; how many it has.
static uint32_t Merge(const muiFontChain* own, muiFontChain* chain, uint8_t* order)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < own->count; i++)
    {
        uint32_t at = 0;
        while (at < chain->count && chain->keys[at] != own->keys[i])
        {
            at++;
        }
        if (at == chain->count)
        {
            if (chain->count == MUI_MAX_FACES)
            {
                continue;
            }
            chain->fonts[chain->count] = own->fonts[i];
            chain->keys[chain->count++] = own->keys[i];
        }
        order[count++] = (uint8_t)at;
    }
    return count;
}

bool muiBuildRunChains(const muiTextService* service, const muiTextBlock* block,
                       const muiComputedTextStyle* style, muiRunChains* out)
{
    muiFontChain own;
    if (!muiBuildChain(service, style, &own))
    {
        return false;
    }
    out->chain = own;
    out->styleCount = block->runStyleCount != 0 ? block->runStyleCount : 1;
    out->orderCount[0] = (uint8_t)own.count;
    for (uint32_t i = 0; i < own.count; i++)
    {
        out->order[0][i] = (uint8_t)i;
    }
    if (block->runStyleCount == 0)
    {
        return true;
    }
    const muiRunStyle* styles = block->runStyles.data;
    for (uint32_t s = 1; s < block->runStyleCount; s++)
    {
        muiComputedTextStyle sub = *style;
        sub.font = styles[s].font;
        sub.weight = styles[s].weight;
        sub.slant = styles[s].slant;
        sub.size = styles[s].size;
        uint32_t count =
            muiBuildChain(service, &sub, &own) ? Merge(&own, &out->chain, out->order[s]) : 0;
        // A span whose font names none draws in the node's.
        if (count == 0)
        {
            memcpy(out->order[s], out->order[0], out->orderCount[0]);
            count = out->orderCount[0];
        }
        out->orderCount[s] = (uint8_t)count;
    }
    // Shaped again when the faces or the runs change.
    uint64_t identity =
        Mix(0xCBF29CE484222325u, out->chain.keys, out->chain.count * sizeof out->chain.keys[0]);
    out->chain.identity = Mix(identity, &block->runKey, sizeof block->runKey);
    return true;
}
