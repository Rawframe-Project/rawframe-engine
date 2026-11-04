// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks: creating them, setting or replacing part of their text,
// holding an input method's composition, and taking its line break
// opportunities and script runs from its analysis (text_analysis.c),
// found into new buffers before the old ones go, so a failure keeps the
// old text. Keys and the default font too.

#include "text_blocks.h"

#include "fpenv.h"
#include "invariant.h"
#include "property.h"
#include "text_analysis.h"
#include "text_block.h"
#include "text_entry.h"
#include "text_service.h"

#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-unicode/encoding.h"

#include <stdint.h>
#include <string.h>

#define TEXT_BLOCK_DEF_COOKIE 0x6D757462u // "mutb"

// Puts an analysis into a block, whose shaping it makes stale.
static void Adopt(const muiAllocator* allocator, muiTextBlock* block, muiTextAnalysis* analysis)
{
    muiFreeBuffer(allocator, &block->text);
    muiFreeBuffer(allocator, &block->breaks);
    muiFreeBuffer(allocator, &block->scripts);
    block->text = analysis->text;
    block->length = analysis->length;
    block->breaks = analysis->breaks;
    block->breakCount = analysis->breakCount;
    block->scripts = analysis->scripts;
    block->scriptCount = analysis->scriptCount;
    block->revision++;
    block->shaped = false;
    // New text: its run styles are made again for it.
    block->runKey = 0;
}

// mui_success, mui_errorInvalid for NULL text with a length, or
// mui_errorLimit for one past MUI_MAX_TEXT_BYTES.
static muiResult TextStatus(const char* text, size_t length)
{
    if (text == nullptr && length != 0)
    {
        return mui_errorInvalid;
    }
    return length <= MUI_MAX_TEXT_BYTES ? mui_success : mui_errorLimit;
}

muiTextBlockDef muiDefaultTextBlockDef(void)
{
    return (muiTextBlockDef){.cookie = TEXT_BLOCK_DEF_COOKIE};
}

static muiResult CreateTextBlock(muiTextService* service, const muiTextBlockDef* def,
                                 muiTextBlockId* blockOut)
{
    muiResult status = service == nullptr || def == nullptr || blockOut == nullptr ||
                               def->cookie != TEXT_BLOCK_DEF_COOKIE
                           ? mui_errorInvalid
                           : TextStatus(def->text, def->length);
    if (status != mui_success)
    {
        return muiCountText(service, status);
    }
    const char* text = def->text;
    size_t length = def->length;
    muiTextBlockStore* store = &service->blocks;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    muiTextAnalysis analysis;
    if (!muiAnalyzeText(&service->allocator, text, (uint32_t)length, &analysis))
    {
        muiPoolGive(&store->pool, slot);
        return mui_errorCapacity;
    }
    muiTextBlock* block = &store->blocks[slot - 1];
    *block = (muiTextBlock){0};
    Adopt(&service->allocator, block, &analysis);
    *blockOut = (muiTextBlockId){slot, muiPoolGeneration(&store->pool, slot)};
    return mui_success;
}

static MUI_NOINLINE muiResult CreateTextBlockWorker(muiTextService* service,
                                                    const muiTextBlockDef* def,
                                                    muiTextBlockId* blockOut)
{
    if (blockOut != nullptr)
    {
        memset(blockOut, 0, sizeof *blockOut);
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = CreateTextBlock(service, def, blockOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiCreateTextBlock(muiTextService* service, const muiTextBlockDef* def,
                             muiTextBlockId* blockOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = CreateTextBlockWorker(service, def, blockOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static uint32_t ResolveBlock(const muiTextService* service, muiTextBlockId blockId)
{
    return muiPoolResolve(&service->blocks.pool, blockId.index1, blockId.generation);
}

static muiResult DestroyTextBlock(muiTextService* service, muiTextBlockId blockId)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return muiRefuseText(service);
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiDropTextBlock(&service->allocator, &service->blocks, slot);
    return mui_success;
}

muiResult muiDestroyTextBlock(muiTextService* service, muiTextBlockId blockId)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = DestroyTextBlock(service, blockId);
        muiLeaveText(service);
    }
    return status;
}

static muiResult TextBlockSetText(muiTextService* service, muiTextBlockId blockId, const char* text,
                                  size_t length)
{
    muiResult status =
        service == nullptr || blockId.index1 == 0 ? mui_errorInvalid : TextStatus(text, length);
    if (status != mui_success)
    {
        return muiCountText(service, status);
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiTextAnalysis analysis;
    if (!muiAnalyzeText(&service->allocator, text, (uint32_t)length, &analysis))
    {
        return mui_errorCapacity;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    Adopt(&service->allocator, block, &analysis);
    block->compositionLength = 0;
    block->spanCount = 0;
    return mui_success;
}

static MUI_NOINLINE muiResult TextBlockSetTextWorker(muiTextService* service,
                                                     muiTextBlockId blockId, const char* text,
                                                     size_t length)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockSetText(service, blockId, text, length);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_SetText(muiTextService* service, muiTextBlockId blockId, const char* text,
                               size_t length)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockSetTextWorker(service, blockId, text, length);
    muiFloatEnvLeave(floatEnv);
    return result;
}

bool muiSetBlockText(muiTextService* service, muiTextBlock* block, const char* text,
                     uint32_t length)
{
    muiTextAnalysis analysis;
    if (!muiAnalyzeText(&service->allocator, text, length, &analysis))
    {
        return false;
    }
    Adopt(&service->allocator, block, &analysis);
    return true;
}

// Where a position goes when the bytes from start up to end become
// length bytes: before, it stays; after, it moves by the change; inside,
// to the new text's end for a span's start and to the range's start for
// its end.
static uint32_t Moved(uint32_t at, uint32_t start, uint32_t end, uint32_t length, bool isStart)
{
    if (at < end && at > start)
    {
        return isStart ? start + length : start;
    }
    if (isStart ? at >= end : at > start)
    {
        return at - (end - start) + length;
    }
    return at;
}

// Moves a block's spans for a replaced range, dropping those left empty.
static void MoveSpans(muiTextBlock* block, uint32_t start, uint32_t end, uint32_t length)
{
    muiTextSpan* spans = block->spans.data;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < block->spanCount; i++)
    {
        muiTextSpan span = spans[i];
        uint32_t first = Moved((uint32_t)span.start, start, end, length, true);
        uint32_t last = Moved((uint32_t)(span.start + span.length), start, end, length, false);
        // A span lay within the old text, so it ends within the new one,
        // whose length the block holds by now.
        MUI_ASSERT(last <= block->length);
        if (last > first)
        {
            span.start = first;
            span.length = last - first;
            spans[kept++] = span;
        }
    }
    block->spanCount = kept;
}

// Marks paragraphs from up to to (in the new text) stale after bytes
// start up to end became length bytes, joined with what was stale
// before, moved through the edit.
static void MarkStale(muiStale* stale, uint32_t start, uint32_t end, uint32_t length, uint32_t from,
                      uint32_t to)
{
    if (stale->on)
    {
        int64_t delta = (int64_t)length - (int64_t)(end - start);
        uint32_t a = stale->start;
        uint32_t b = stale->end;
        a = a <= start ? a : (a >= end ? (uint32_t)((int64_t)a + delta) : start);
        b = b <= start ? b : (b >= end ? (uint32_t)((int64_t)b + delta) : start + length);
        from = a < from ? a : from;
        to = b > to ? b : to;
    }
    *stale = (muiStale){true, from, to};
}

// Replaces bytes start up to end of a block with length bytes, finding
// again the breaks and script runs of the paragraphs the edit reaches
// alone: from past the last mandatory break before it up to the first
// paragraph's end past it, whose separator the edit leaves as it was;
// the rest moves by the change.
static bool ReplaceRange(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                         const char* text, uint32_t length)
{
    const muiAllocator* allocator = &service->allocator;
    muiTextAnalysis analysis;
    uint32_t from = 0;
    uint32_t to = 0;
    if (!muiAnalyzeReplace(allocator, block, start, end, text, length, &analysis, &from, &to))
    {
        return false;
    }
    // Spans' run styles are found for the whole text again, so a block
    // with spans is shaped whole. Words kept for the text before follow
    // the edit; word breaks never cross a paragraph's.
    bool keep = block->shaped && block->spanCount == 0;
    bool keepWords = block->accessWords.made && block->accessWords.revision == block->revision;
    Adopt(allocator, block, &analysis);
    if (keepWords)
    {
        MarkStale(&block->accessWords.stale, start, end, length, from, to);
        block->accessWords.revision = block->revision;
    }
    if (keep)
    {
        block->shaped = true;
        MarkStale(&block->stale, start, end, length, from, to);
        // Lines kept from the shaping follow it; those of an older one are
        // broken whole.
        for (int i = 0; i < MUI_LINE_CACHES; i++)
        {
            MarkStale(&block->lineCaches[i].stale, start, end, length, from, to);
        }
    }
    return true;
}

// Whether replacing start up to end with length bytes keeps the text
// within its limit.
static bool Fits(const muiTextBlock* block, uint32_t start, uint32_t end, size_t length)
{
    return block->length - (end - start) <= MUI_MAX_TEXT_BYTES - length;
}

static muiResult TextBlockReplace(muiTextService* service, muiTextBlockId blockId, uint32_t start,
                                  uint32_t end, const char* text, size_t length)
{
    muiResult status = service == nullptr || blockId.index1 == 0 || start > end
                           ? mui_errorInvalid
                           : TextStatus(text, length);
    if (status != mui_success)
    {
        return muiCountText(service, status);
    }
    muiTextBlock* block = muiResolveTextBlock(service, blockId);
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    return muiReplaceBlockText(service, block, start, end, text, length);
}

static MUI_NOINLINE muiResult TextBlockReplaceWorker(muiTextService* service,
                                                     muiTextBlockId blockId, size_t start,
                                                     size_t end, const char* text, size_t length)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockReplace(service, blockId, muiTextOffsetOf(start), muiTextOffsetOf(end),
                                  text, length);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Replace(muiTextService* service, muiTextBlockId blockId, size_t start,
                               size_t end, const char* text, size_t length)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockReplaceWorker(service, blockId, start, end, text, length);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiTextBlock* muiResolveTextBlock(const muiTextService* service, muiTextBlockId blockId)
{
    uint32_t slot = ResolveBlock(service, blockId);
    return slot != 0 ? &service->blocks.blocks[slot - 1] : nullptr;
}

muiResult muiBlockTextStatus(const muiTextBlock* block, uint32_t start, uint32_t end, size_t length)
{
    if (start > end || end > block->length)
    {
        return mui_errorInvalid;
    }
    return length <= MUI_MAX_TEXT_BYTES && Fits(block, start, end, length) ? mui_success
                                                                           : mui_errorLimit;
}

muiResult muiReplaceBlockText(muiTextService* service, muiTextBlock* block, uint32_t start,
                              uint32_t end, const char* text, size_t length)
{
    muiResult fits = muiBlockTextStatus(block, start, end, length);
    if (fits != mui_success)
    {
        return muiCountText(service, fits);
    }
    if (!ReplaceRange(service, block, start, end, text, (uint32_t)length))
    {
        return mui_errorCapacity;
    }
    MoveSpans(block, start, end, (uint32_t)length);
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
    // One before the range stays, one after it moves, so one left shown
    // ends within the text.
    MUI_ASSERT(block->compositionLength == 0 ||
               block->compositionStart + block->compositionLength <= block->length);
    return mui_success;
}

muiResult muiSetComposition(muiTextService* service, muiTextBlockId blockId, size_t offset,
                            const char* text, size_t length, const muiCompositionSegment* segments,
                            uint32_t segmentCount)
{
    muiResult status =
        service == nullptr || blockId.index1 == 0 ? mui_errorInvalid : TextStatus(text, length);
    status = status == mui_success ? muiSegmentsStatus(segments, segmentCount, length) : status;
    if (status != mui_success)
    {
        return muiCountText(service, status);
    }
    muiTextBlock* block = muiResolveTextBlock(service, blockId);
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    uint32_t start = block->compositionLength != 0 ? block->compositionStart
                     : offset < block->length      ? (uint32_t)offset
                                                   : block->length;
    uint32_t end = start + block->compositionLength;
    return Fits(block, start, end, length)
               ? muiComposeOver(service, block, start, end, text, (uint32_t)length, segments,
                                segmentCount)
               : muiCountText(service, mui_errorLimit);
}

muiResult muiComposeOver(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                         const char* text, uint32_t length, const muiCompositionSegment* segments,
                         uint32_t segmentCount)
{
    // The segments shown stay while memory for the new ones is found.
    size_t kept = block->segmentCount * sizeof(muiCompositionSegment);
    size_t needed = segmentCount * sizeof(muiCompositionSegment);
    if (!muiReserveKeeping(&service->allocator, &block->segments, needed > kept ? needed : kept,
                           kept) ||
        !ReplaceRange(service, block, start, end, text, length))
    {
        return mui_errorCapacity;
    }
    MoveSpans(block, start, end, length);
    if (segmentCount != 0)
    {
        memcpy(block->segments.data, segments, segmentCount * sizeof *segments);
    }
    block->compositionStart = start;
    block->compositionLength = length;
    block->segmentCount = segmentCount;
    return mui_success;
}

static MUI_NOINLINE muiResult TextBlockSetCompositionWorker(muiTextService* service,
                                                            muiTextBlockId blockId, size_t offset,
                                                            const char* text, size_t length,
                                                            const muiCompositionSegment* segments,
                                                            uint32_t segmentCount)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiSetComposition(service, blockId, offset, text, length, segments, segmentCount);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_SetComposition(muiTextService* service, muiTextBlockId blockId,
                                      size_t offset, const char* text, size_t length,
                                      const muiCompositionSegment* segments, uint32_t segmentCount)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockSetCompositionWorker(service, blockId, offset, text, length,
                                                     segments, segmentCount);
    muiFloatEnvLeave(floatEnv);
    return result;
}

bool muiIsCharacterStartIn(const char* bytes, uint32_t length, uint32_t at)
{
    const unsigned char* text = (const unsigned char*)bytes;
    if (at >= length || (text[at] & 0xC0u) != 0x80u)
    {
        return at <= length;
    }
    // A continuation byte: inside the sequence of the lead before it, if
    // that lead's sequence reaches it, as no sequence is over four bytes.
    uint32_t lead = at;
    while (lead > 0 && at - lead < 3 && (text[lead - 1] & 0xC0u) == 0x80u)
    {
        lead--;
    }
    if (lead == 0 || (text[lead - 1] & 0xC0u) == 0x80u)
    {
        return true;
    }
    lead--;
    uint32_t point = 0;
    size_t size = 1;
    (void)muniDecodeUtf8(bytes + lead, length - lead, &point, &size);
    return lead + size <= at;
}

// The text properties a span may set: what painting reads, the font,
// size, weight and slant its runs are shaped in, and their baseline
// shift.
#define SPAN_PROPERTIES                                                                            \
    (MUI_PROPERTY_BIT(mui_propertyTextColor) | MUI_PROPERTY_BIT(mui_propertyTextDecoration) |      \
     MUI_PROPERTY_BIT(mui_propertyTextDecorationColor) | MUI_PROPERTY_BIT(mui_propertyFont) |      \
     MUI_PROPERTY_BIT(mui_propertyFontSize) | MUI_PROPERTY_BIT(mui_propertyFontWeight) |           \
     MUI_PROPERTY_BIT(mui_propertyFontSlant) | MUI_PROPERTY_BIT(mui_propertyTextBaselineShift))

// mui_success, mui_errorInvalid or mui_errorLimit for a block's spans.
static muiResult SpansStatus(const muiTextBlock* block, const muiTextSpan* spans, uint32_t count)
{
    if (count > MUI_MAX_TEXT_SPANS || (spans == nullptr && count != 0))
    {
        return count > MUI_MAX_TEXT_SPANS ? mui_errorLimit : mui_errorInvalid;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const muiTextSpan* span = &spans[i];
        if (span->length == 0 || span->start > block->length ||
            span->length > block->length - span->start ||
            !muiIsCharacterStart(block, (uint32_t)span->start) ||
            !muiIsCharacterStart(block, (uint32_t)(span->start + span->length)) ||
            (span->mask & ~(muiPropertyMask)SPAN_PROPERTIES) != 0 ||
            !muiArePropertiesValid((muiConstValuesRef){nullptr, nullptr, &span->style, nullptr},
                                   muiPropertiesOf(mui_groupText, span->mask)))
        {
            return mui_errorInvalid;
        }
    }
    return mui_success;
}

static muiResult TextBlockSetSpans(muiTextService* service, muiTextBlockId blockId,
                                   const muiTextSpan* spans, uint32_t count)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return muiRefuseText(service);
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    muiResult spansStatus = SpansStatus(block, spans, count);
    if (spansStatus != mui_success)
    {
        return muiCountText(service, spansStatus);
    }
    if (!muiReserve(&service->allocator, &block->spans, count * sizeof(muiTextSpan)))
    {
        return mui_errorCapacity;
    }
    if (count != 0)
    {
        memcpy(block->spans.data, spans, count * sizeof *spans);
    }
    block->spanCount = count;
    return mui_success;
}

static MUI_NOINLINE muiResult TextBlockSetSpansWorker(muiTextService* service,
                                                      muiTextBlockId blockId,
                                                      const muiTextSpan* spans, uint32_t count)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockSetSpans(service, blockId, spans, count);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_SetSpans(muiTextService* service, muiTextBlockId blockId,
                                const muiTextSpan* spans, uint32_t count)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockSetSpansWorker(service, blockId, spans, count);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiTextBlock_GetSpans(const muiTextService* service, muiTextBlockId blockId,
                                const muiTextSpan** spansOut, uint32_t* countOut)
{
    if (spansOut != nullptr)
    {
        *spansOut = nullptr;
    }
    if (countOut != nullptr)
    {
        *countOut = 0;
    }
    if (service == nullptr || blockId.index1 == 0 || spansOut == nullptr || countOut == nullptr)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiTextBlock* block = &service->blocks.blocks[slot - 1];
    *spansOut = block->spanCount != 0 ? block->spans.data : nullptr;
    *countOut = block->spanCount;
    return mui_success;
}

muiResult muiEndComposition(muiTextService* service, muiTextBlockId blockId)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return muiRefuseText(service);
    }
    uint32_t slot = ResolveBlock(service, blockId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    // Text kept is the block's own from now on, a change an editor's
    // history never recorded: a new revision makes it start over.
    if (block->compositionLength != 0)
    {
        block->compositionLength = 0;
        block->revision++;
    }
    return mui_success;
}

muiResult muiTextBlock_EndComposition(muiTextService* service, muiTextBlockId blockId)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiEndComposition(service, blockId);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_GetComposition(const muiTextService* service, muiTextBlockId blockId,
                                      size_t* startOut, size_t* lengthOut)
{
    if (startOut != nullptr)
    {
        *startOut = 0;
    }
    if (lengthOut != nullptr)
    {
        *lengthOut = 0;
    }
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
    if (textOut != nullptr)
    {
        *textOut = nullptr;
    }
    if (lengthOut != nullptr)
    {
        *lengthOut = 0;
    }
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

uint64_t muiTextBlock_GetKey(muiTextBlockId blockId)
{
    return (uint64_t)blockId.generation << 32 | blockId.index1;
}

uint64_t muiTextService_GetLayoutFailureCount(const muiTextService* service)
{
    return service != nullptr ? service->failures : 0;
}
