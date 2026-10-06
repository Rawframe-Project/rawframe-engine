// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks (record mui-0006): creating them, setting or replacing
// part of their text, holding an input method's composition, and
// finding its line break opportunities and script runs once, when it is
// set. A new text is analyzed into new buffers before the old ones go,
// so a failure keeps the old text. Keys and the default font too.

#include "font_instance.h"
#include "text_block.h"
#include "text_service.h"

#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-unicode/script.h"
#include "maul-unicode/segment.h"

#include <stdint.h>
#include <string.h>

enum
{
    // Texts are indexed by 32-bit offsets, with room for one past the
    // end.
    MAX_TEXT = 0x7FFFFFFF
};

// A block's text and what was found in it, made apart from the block.
typedef struct Analysis
{
    muiBuffer text;
    muiBuffer breaks;
    uint32_t breakCount;
    muiBuffer scripts;
    uint32_t scriptCount;
} Analysis;

static void FreeAnalysis(const muiAllocator* allocator, Analysis* analysis)
{
    muiFreeBuffer(allocator, &analysis->text);
    muiFreeBuffer(allocator, &analysis->breaks);
    muiFreeBuffer(allocator, &analysis->scripts);
}

// The line break opportunities, through scratch arrays of Maul Unicode's
// types.
static bool FindBreaks(muiTextService* service, const char* text, uint32_t length,
                       Analysis* analysis)
{
    size_t count = 0;
    if (muniFindLineBreaks(text, length, nullptr, nullptr, 0, &count) != muni_errorCapacity ||
        count == 0)
    {
        return true;
    }
    const muiAllocator* allocator = &service->allocator;
    muiBuffer offsets = {0};
    muiBuffer mandatory = {0};
    bool fits = muiReserve(allocator, &offsets, count * sizeof(size_t)) &&
                muiReserve(allocator, &mandatory, count * sizeof(bool)) &&
                muiReserve(allocator, &analysis->breaks, count * sizeof(muiTextBreak));
    if (fits)
    {
        size_t found = 0;
        (void)muniFindLineBreaks(text, length, offsets.data, mandatory.data, count, &found);
        const size_t* at = offsets.data;
        const bool* must = mandatory.data;
        muiTextBreak* breaks = analysis->breaks.data;
        for (size_t i = 0; i < count; i++)
        {
            breaks[i] = (muiTextBreak){(uint32_t)at[i], must[i] ? 1u : 0u};
        }
        analysis->breakCount = (uint32_t)count;
    }
    muiFreeBuffer(allocator, &offsets);
    muiFreeBuffer(allocator, &mandatory);
    return fits;
}

static bool FindScripts(muiTextService* service, const char* text, uint32_t length,
                        Analysis* analysis)
{
    size_t count = 0;
    if (muniFindScriptRuns(text, length, nullptr, 0, &count) != muni_errorCapacity || count == 0)
    {
        return true;
    }
    const muiAllocator* allocator = &service->allocator;
    muiBuffer runs = {0};
    bool fits = muiReserve(allocator, &runs, count * sizeof(muniScriptRun)) &&
                muiReserve(allocator, &analysis->scripts, count * sizeof(muiTextScript));
    if (fits)
    {
        size_t found = 0;
        (void)muniFindScriptRuns(text, length, runs.data, count, &found);
        const muniScriptRun* run = runs.data;
        muiTextScript* scripts = analysis->scripts.data;
        for (size_t i = 0; i < count; i++)
        {
            scripts[i] = (muiTextScript){(uint32_t)run[i].end, run[i].script};
        }
        analysis->scriptCount = (uint32_t)count;
    }
    muiFreeBuffer(allocator, &runs);
    return fits;
}

// A piece of a text being set: bytes, and how many.
typedef struct Piece
{
    const char* bytes;
    uint32_t length;
} Piece;

// Analyzes the text the pieces make in turn, copied first, so a piece may
// be the block's own old text.
static bool Analyze(muiTextService* service, const Piece* pieces, uint32_t count,
                    Analysis* analysis)
{
    *analysis = (Analysis){0};
    uint32_t length = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        length += pieces[i].length;
    }
    // At least a byte, so an empty text has a buffer too.
    if (!muiReserve(&service->allocator, &analysis->text, length + 1u))
    {
        return false;
    }
    char* copy = analysis->text.data;
    for (uint32_t i = 0, at = 0; i < count; at += pieces[i].length, i++)
    {
        if (pieces[i].length != 0)
        {
            memcpy(copy + at, pieces[i].bytes, pieces[i].length);
        }
    }
    if (!FindBreaks(service, copy, length, analysis) ||
        !FindScripts(service, copy, length, analysis))
    {
        FreeAnalysis(&service->allocator, analysis);
        return false;
    }
    return true;
}

// Puts an analysis into a block, whose shaping it makes stale.
static void Adopt(const muiAllocator* allocator, muiTextBlock* block, Analysis* analysis,
                  uint32_t length)
{
    muiFreeBuffer(allocator, &block->text);
    muiFreeBuffer(allocator, &block->breaks);
    muiFreeBuffer(allocator, &block->scripts);
    block->text = analysis->text;
    block->length = length;
    block->breaks = analysis->breaks;
    block->breakCount = analysis->breakCount;
    block->scripts = analysis->scripts;
    block->scriptCount = analysis->scriptCount;
    block->shaped = false;
}

static bool IsTextValid(const char* text, size_t length)
{
    return (text != nullptr || length == 0) && length <= MAX_TEXT;
}

muiResult muiCreateTextBlock(muiTextService* service, const char* text, size_t length,
                             muiTextBlockId* blockOut)
{
    if (blockOut != nullptr)
    {
        *blockOut = (muiTextBlockId){0};
    }
    if (service == nullptr || blockOut == nullptr || !IsTextValid(text, length))
    {
        return mui_errorInvalid;
    }
    muiTextBlockStore* store = &service->blocks;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    Analysis analysis;
    const Piece piece = {text, (uint32_t)length};
    if (!Analyze(service, &piece, 1, &analysis))
    {
        muiPoolGive(&store->pool, slot);
        return mui_errorCapacity;
    }
    muiTextBlock* block = &store->blocks[slot - 1];
    *block = (muiTextBlock){0};
    Adopt(&service->allocator, block, &analysis, (uint32_t)length);
    *blockOut = (muiTextBlockId){slot, muiPoolGeneration(&store->pool, slot)};
    return mui_success;
}

static uint32_t ResolveBlock(const muiTextService* service, muiTextBlockId blockId)
{
    return muiPoolResolve(&service->blocks.pool, blockId.index1, blockId.generation);
}

muiResult muiDestroyTextBlock(muiTextService* service, muiTextBlockId blockId)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiReleaseTextBlock(&service->allocator, &service->blocks.blocks[slot - 1]);
    muiPoolGive(&service->blocks.pool, slot);
    return mui_success;
}

muiResult muiTextBlock_SetText(muiTextService* service, muiTextBlockId blockId, const char* text,
                               size_t length)
{
    if (service == nullptr || blockId.index1 == 0 || !IsTextValid(text, length))
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    Analysis analysis;
    const Piece piece = {text, (uint32_t)length};
    if (!Analyze(service, &piece, 1, &analysis))
    {
        return mui_errorCapacity;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    Adopt(&service->allocator, block, &analysis, (uint32_t)length);
    block->compositionLength = 0;
    return mui_success;
}

// Replaces the bytes of a block's text from start up to end, both within
// it, with a text, which may be part of the old; false when memory runs
// out, which keeps the old.
static bool ReplaceRange(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                         const char* text, uint32_t length)
{
    const char* old = block->text.data;
    const Piece pieces[3] = {{old, start}, {text, length}, {old + end, block->length - end}};
    Analysis analysis;
    if (!Analyze(service, pieces, 3, &analysis))
    {
        return false;
    }
    Adopt(&service->allocator, block, &analysis, block->length - (end - start) + length);
    return true;
}

// Whether replacing start up to end with length bytes keeps the text
// within its limit.
static bool Fits(const muiTextBlock* block, uint32_t start, uint32_t end, size_t length)
{
    return block->length - (end - start) <= MAX_TEXT - length;
}

muiResult muiTextBlock_Replace(muiTextService* service, muiTextBlockId blockId, uint32_t start,
                               uint32_t end, const char* text, size_t length)
{
    if (service == nullptr || blockId.index1 == 0 || !IsTextValid(text, length) || start > end)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    if (end > block->length || !Fits(block, start, end, length))
    {
        return mui_errorInvalid;
    }
    if (!ReplaceRange(service, block, start, end, text, (uint32_t)length))
    {
        return mui_errorCapacity;
    }
    // A composition after the range moves with it; one it overlaps ends.
    uint32_t compositionEnd = block->compositionStart + block->compositionLength;
    if (end <= block->compositionStart)
    {
        block->compositionStart = block->compositionStart - (end - start) + (uint32_t)length;
    }
    else if (start < compositionEnd)
    {
        block->compositionLength = 0;
    }
    return mui_success;
}

static bool AreSegmentsValid(const muiCompositionSegment* segments, uint32_t count, size_t length)
{
    if ((segments == nullptr && count != 0) || count > MUI_MAX_COMPOSITION_SEGMENTS)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const muiCompositionSegment* segment = &segments[i];
        if (segment->start > length || segment->length > length - segment->start ||
            segment->style > mui_compositionConverted)
        {
            return false;
        }
    }
    return true;
}

muiResult muiTextBlock_SetComposition(muiTextService* service, muiTextBlockId blockId,
                                      uint32_t offset, const char* text, size_t length,
                                      const muiCompositionSegment* segments, uint32_t segmentCount)
{
    if (service == nullptr || blockId.index1 == 0 || !IsTextValid(text, length) ||
        !AreSegmentsValid(segments, segmentCount, length))
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    uint32_t start = block->compositionLength != 0 ? block->compositionStart
                     : offset < block->length      ? offset
                                                   : block->length;
    uint32_t end = start + block->compositionLength;
    if (!Fits(block, start, end, length))
    {
        return mui_errorInvalid;
    }
    if (!muiReserve(&service->allocator, &block->segments,
                    segmentCount * sizeof(muiCompositionSegment)) ||
        !ReplaceRange(service, block, start, end, text, (uint32_t)length))
    {
        return mui_errorCapacity;
    }
    if (segmentCount != 0)
    {
        memcpy(block->segments.data, segments, segmentCount * sizeof *segments);
    }
    block->compositionStart = start;
    block->compositionLength = (uint32_t)length;
    block->segmentCount = segmentCount;
    return mui_success;
}

muiResult muiTextBlock_EndComposition(muiTextService* service, muiTextBlockId blockId)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    service->blocks.blocks[slot - 1].compositionLength = 0;
    return mui_success;
}

muiResult muiTextBlock_GetComposition(const muiTextService* service, muiTextBlockId blockId,
                                      uint32_t* startOut, uint32_t* lengthOut)
{
    if (service == nullptr || blockId.index1 == 0 || startOut == nullptr || lengthOut == nullptr)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiTextBlock* block = &service->blocks.blocks[slot - 1];
    *startOut = block->compositionStart;
    *lengthOut = block->compositionLength;
    return mui_success;
}

muiResult muiTextBlock_GetText(const muiTextService* service, muiTextBlockId blockId,
                               const char** textOut, size_t* lengthOut)
{
    if (service == nullptr || blockId.index1 == 0 || textOut == nullptr || lengthOut == nullptr)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiTextBlock* block = &service->blocks.blocks[slot - 1];
    *textOut = block->text.data;
    *lengthOut = block->length;
    return mui_success;
}

bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, const char** textOut,
                     size_t* lengthOut)
{
    (void)nodeId;
    const muiTextHost* host = user;
    const muiTextBlockId blockId = {(uint32_t)hostKey, (uint32_t)(hostKey >> 32)};
    return host != nullptr &&
           muiTextBlock_GetText(host->service, blockId, textOut, lengthOut) == mui_success;
}

uint64_t muiTextBlock_GetKey(muiTextBlockId blockId)
{
    return (uint64_t)blockId.generation << 32 | blockId.index1;
}

uint64_t muiFont_GetKey(muiFontId fontId)
{
    return fontId.index1 != 0 ? muiKeyOf(fontId.index1, fontId.generation) : 0;
}

uint64_t muiFontFamily_GetKey(muiFontFamilyId familyId)
{
    return familyId.index1 != 0 ? MUI_FAMILY_BIT | muiKeyOf(familyId.index1, familyId.generation)
                                : 0;
}

muiResult muiSetDefaultFont(muiTextService* service, muiFontId fontId)
{
    if (service == nullptr)
    {
        return mui_errorInvalid;
    }
    if (fontId.index1 != 0 && !muiFont_IsValid(service, fontId))
    {
        return mui_errorStale;
    }
    service->defaultFont = fontId;
    return mui_success;
}

uint64_t muiGetTextServiceFailures(const muiTextService* service)
{
    return service != nullptr ? service->failures : 0;
}
