// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks: creating them, setting or replacing part of their text,
// holding an input method's composition, and finding its line break
// opportunities and script runs once, when it is set. A new text is
// analyzed into new buffers before the old ones go, so a failure keeps the
// old text. Keys and the default font too.

#include "text_blocks.h"

#include "fpenv.h"
#include "property.h"
#include "text_block.h"
#include "text_entry.h"
#include "text_service.h"

#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/script.h"
#include "maul-unicode/segment.h"

#include <stdint.h>
#include <string.h>

#define TEXT_BLOCK_DEF_COOKIE 0x6D757462u // "mutb"

// A block's text and what was found in it, made apart from the block.
typedef struct Analysis
{
    muiBuffer text;
    muiBuffer breaks;
    uint32_t breakCount;
    muiBuffer scripts;
    uint32_t scriptCount;
} Analysis;

// Room Maul Unicode's answers for one paragraph take, kept across the
// paragraphs of an analysis.
typedef struct Scratch
{
    muiBuffer offsets;
    muiBuffer mandatory;
    muiBuffer runs;
} Scratch;

static void FreeAnalysis(const muiAllocator* allocator, Analysis* analysis)
{
    muiFreeBuffer(allocator, &analysis->text);
    muiFreeBuffer(allocator, &analysis->breaks);
    muiFreeBuffer(allocator, &analysis->scripts);
}

static void FreeScratch(const muiAllocator* allocator, Scratch* scratch)
{
    muiFreeBuffer(allocator, &scratch->offsets);
    muiFreeBuffer(allocator, &scratch->mandatory);
    muiFreeBuffer(allocator, &scratch->runs);
}

// Appends a paragraph's line break opportunities, its offsets from at.
static bool FindBreaks(const muiAllocator* allocator, Scratch* scratch, const char* text,
                       uint32_t at, uint32_t length, Analysis* analysis)
{
    size_t count = 0;
    if (muniFindLineBreaks(text + at, length, nullptr, nullptr, 0, &count) != muni_errorCapacity ||
        count == 0)
    {
        return true;
    }
    size_t kept = analysis->breakCount;
    if (!muiReserve(allocator, &scratch->offsets, count * sizeof(size_t)) ||
        !muiReserve(allocator, &scratch->mandatory, count * sizeof(bool)) ||
        !muiReserveKeeping(allocator, &analysis->breaks, (kept + count) * sizeof(muiTextBreak),
                           kept * sizeof(muiTextBreak)))
    {
        return false;
    }
    size_t found = 0;
    (void)muniFindLineBreaks(text + at, length, scratch->offsets.data, scratch->mandatory.data,
                             count, &found);
    const size_t* offsets = scratch->offsets.data;
    const bool* must = scratch->mandatory.data;
    muiTextBreak* breaks = (muiTextBreak*)analysis->breaks.data + kept;
    for (size_t i = 0; i < count; i++)
    {
        breaks[i] = (muiTextBreak){at + (uint32_t)offsets[i], must[i] ? 1u : 0u};
    }
    analysis->breakCount += (uint32_t)count;
    return true;
}

// Appends a paragraph's script runs, their ends from at.
static bool FindScripts(const muiAllocator* allocator, Scratch* scratch, const char* text,
                        uint32_t at, uint32_t length, Analysis* analysis)
{
    size_t count = 0;
    if (muniFindScriptRuns(text + at, length, nullptr, 0, &count) != muni_errorCapacity ||
        count == 0)
    {
        return true;
    }
    size_t kept = analysis->scriptCount;
    if (!muiReserve(allocator, &scratch->runs, count * sizeof(muniScriptRun)) ||
        !muiReserveKeeping(allocator, &analysis->scripts, (kept + count) * sizeof(muiTextScript),
                           kept * sizeof(muiTextScript)))
    {
        return false;
    }
    size_t found = 0;
    (void)muniFindScriptRuns(text + at, length, scratch->runs.data, count, &found);
    const muniScriptRun* runs = scratch->runs.data;
    muiTextScript* scripts = (muiTextScript*)analysis->scripts.data + kept;
    for (size_t i = 0; i < count; i++)
    {
        scripts[i] = (muiTextScript){at + (uint32_t)runs[i].end, runs[i].script};
    }
    analysis->scriptCount += (uint32_t)count;
    return true;
}

// Appends the breaks and script runs of a text's paragraphs from start
// up to end, each paragraph found alone (UAX #14 breaks and UAX #24
// runs never reach across a mandatory break), so that an edit finds a
// paragraph's again as a whole text's analysis does.
static bool AnalyzeParagraphs(const muiAllocator* allocator, const char* text, uint32_t start,
                              uint32_t end, Analysis* analysis)
{
    Scratch scratch = {0};
    bool fits = true;
    for (uint32_t at = start; fits && at < end;)
    {
        uint32_t next = muiParagraphEnd(text, end, at);
        fits = FindBreaks(allocator, &scratch, text, at, next - at, analysis) &&
               FindScripts(allocator, &scratch, text, at, next - at, analysis);
        at = next;
    }
    FreeScratch(allocator, &scratch);
    return fits;
}

// A piece of a text being set: bytes, and how many.
typedef struct Piece
{
    const char* bytes;
    uint32_t length;
} Piece;

// Copies the text the pieces make in turn into an analysis, so a piece
// may be the block's own old text.
static bool Join(const muiAllocator* allocator, const Piece* pieces, uint32_t count,
                 Analysis* analysis, uint32_t* lengthOut)
{
    uint32_t length = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        length += pieces[i].length;
    }
    // At least a byte, so an empty text has a buffer too.
    if (!muiReserve(allocator, &analysis->text, length + 1u))
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
    *lengthOut = length;
    return true;
}

// Analyzes the text the pieces make in turn, copied first.
static bool Analyze(muiTextService* service, const Piece* pieces, uint32_t count,
                    Analysis* analysis)
{
    *analysis = (Analysis){0};
    uint32_t length = 0;
    if (!Join(&service->allocator, pieces, count, analysis, &length) ||
        !AnalyzeParagraphs(&service->allocator, analysis->text.data, 0, length, analysis))
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
    muiReleaseTextBlock(&service->allocator, &service->blocks.blocks[slot - 1]);
    muiPoolGive(&service->blocks.pool, slot);
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
    Analysis analysis;
    const Piece piece = {text, (uint32_t)length};
    if (!Analyze(service, &piece, 1, &analysis))
    {
        return mui_errorCapacity;
    }
    muiTextBlock* block = &service->blocks.blocks[slot - 1];
    Adopt(&service->allocator, block, &analysis, (uint32_t)length);
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
    Analysis analysis;
    const Piece piece = {text, length};
    if (!Analyze(service, &piece, 1, &analysis))
    {
        return false;
    }
    Adopt(&service->allocator, block, &analysis, length);
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
        uint32_t first = Moved(span.start, start, end, length, true);
        uint32_t last = Moved(span.start + span.length, start, end, length, false);
        if (last > first)
        {
            span.start = first;
            span.length = last - first;
            spans[kept++] = span;
        }
    }
    block->spanCount = kept;
}

// Replaces the bytes of a block's text from start up to end, both within
// it, with a text, which may be part of the old; false when memory runs
// out, which keeps the old.
// Where the paragraph an edit at start lands in begins: past the last
// mandatory break before it, whose paragraph the edit leaves as it was.
static uint32_t ParagraphBefore(const muiTextBlock* block, uint32_t start)
{
    const muiTextBreak* breaks = block->breaks.data;
    uint32_t low = 0;
    uint32_t high = block->breakCount;
    // The first break at start or past it.
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        low = breaks[middle].offset < start ? middle + 1 : low;
        high = breaks[middle].offset < start ? high : middle;
    }
    while (low > 0 && breaks[low - 1].mandatory == 0)
    {
        low--;
    }
    return low > 0 ? breaks[low - 1].offset : 0;
}

// How many of a table's entries lie at or before an offset.
static uint32_t CountBreaksTo(const muiTextBreak* breaks, uint32_t count, uint32_t offset)
{
    uint32_t low = 0;
    while (low < count && breaks[low].offset <= offset)
    {
        low++;
    }
    return low;
}

static uint32_t CountScriptsTo(const muiTextScript* scripts, uint32_t count, uint32_t offset)
{
    uint32_t low = 0;
    while (low < count && scripts[low].end <= offset)
    {
        low++;
    }
    return low;
}

// memcpy, which takes no null pointer even for no bytes.
static void Copy(void* to, const void* from, size_t bytes)
{
    if (bytes != 0)
    {
        memcpy(to, from, bytes);
    }
}

// The block's tables with its old paragraphs from 0 up to from and from
// oldTo on, moved by delta, around a region's: in analysis, made whole.
static bool Splice(const muiAllocator* allocator, const muiTextBlock* block, uint32_t from,
                   uint32_t oldTo, int64_t delta, Analysis* analysis)
{
    Analysis region = *analysis;
    analysis->breaks = (muiBuffer){0};
    analysis->scripts = (muiBuffer){0};
    const muiTextBreak* breaks = block->breaks.data;
    const muiTextScript* scripts = block->scripts.data;
    uint32_t breaksBefore = CountBreaksTo(breaks, block->breakCount, from);
    uint32_t breaksAfter = block->breakCount - CountBreaksTo(breaks, block->breakCount, oldTo);
    uint32_t scriptsBefore = CountScriptsTo(scripts, block->scriptCount, from);
    uint32_t scriptsAfter = block->scriptCount - CountScriptsTo(scripts, block->scriptCount, oldTo);
    analysis->breakCount = breaksBefore + region.breakCount + breaksAfter;
    analysis->scriptCount = scriptsBefore + region.scriptCount + scriptsAfter;
    bool fits =
        muiReserve(allocator, &analysis->breaks, analysis->breakCount * sizeof(muiTextBreak) + 1) &&
        muiReserve(allocator, &analysis->scripts,
                   analysis->scriptCount * sizeof(muiTextScript) + 1);
    if (fits)
    {
        muiTextBreak* outBreaks = analysis->breaks.data;
        Copy(outBreaks, breaks, breaksBefore * sizeof(muiTextBreak));
        Copy(outBreaks + breaksBefore, region.breaks.data,
             region.breakCount * sizeof(muiTextBreak));
        for (uint32_t i = 0; i < breaksAfter; i++)
        {
            muiTextBreak moved = breaks[block->breakCount - breaksAfter + i];
            moved.offset = (uint32_t)((int64_t)moved.offset + delta);
            outBreaks[breaksBefore + region.breakCount + i] = moved;
        }
        muiTextScript* outScripts = analysis->scripts.data;
        Copy(outScripts, scripts, scriptsBefore * sizeof(muiTextScript));
        Copy(outScripts + scriptsBefore, region.scripts.data,
             region.scriptCount * sizeof(muiTextScript));
        for (uint32_t i = 0; i < scriptsAfter; i++)
        {
            muiTextScript moved = scripts[block->scriptCount - scriptsAfter + i];
            moved.end = (uint32_t)((int64_t)moved.end + delta);
            outScripts[scriptsBefore + region.scriptCount + i] = moved;
        }
    }
    muiFreeBuffer(allocator, &region.breaks);
    muiFreeBuffer(allocator, &region.scripts);
    return fits;
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
    const char* old = block->text.data;
    const Piece pieces[3] = {{old, start}, {text, length}, {old + end, block->length - end}};
    Analysis analysis = {0};
    uint32_t total = 0;
    if (!Join(allocator, pieces, 3, &analysis, &total))
    {
        return false;
    }
    uint32_t from = ParagraphBefore(block, start);
    uint32_t to = muiParagraphEnd(analysis.text.data, total, start + length);
    int64_t delta = (int64_t)total - (int64_t)block->length;
    if (!AnalyzeParagraphs(allocator, analysis.text.data, from, to, &analysis) ||
        !Splice(allocator, block, from, (uint32_t)((int64_t)to - delta), delta, &analysis))
    {
        FreeAnalysis(allocator, &analysis);
        return false;
    }
    // Spans' run styles are found for the whole text again, so a block
    // with spans is shaped whole. Words kept for the text before follow
    // the edit; word breaks never cross a paragraph's.
    bool keep = block->shaped && block->spanCount == 0;
    bool keepWords = block->accessWords.made && block->accessWords.revision == block->revision;
    Adopt(allocator, block, &analysis, total);
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
                                                     muiTextBlockId blockId, uint32_t start,
                                                     uint32_t end, const char* text, size_t length)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockReplace(service, blockId, start, end, text, length);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Replace(muiTextService* service, muiTextBlockId blockId, uint32_t start,
                               uint32_t end, const char* text, size_t length)
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
    return mui_success;
}

// mui_success, mui_errorInvalid or mui_errorLimit for a composition's
// segments.
static muiResult SegmentsStatus(const muiCompositionSegment* segments, uint32_t count,
                                size_t length)
{
    if (count > MUI_MAX_COMPOSITION_SEGMENTS || (segments == nullptr && count != 0))
    {
        return count > MUI_MAX_COMPOSITION_SEGMENTS ? mui_errorLimit : mui_errorInvalid;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const muiCompositionSegment* segment = &segments[i];
        if (segment->start > length || segment->length > length - segment->start ||
            segment->style > mui_compositionConverted)
        {
            return mui_errorInvalid;
        }
    }
    return mui_success;
}

muiResult muiSetComposition(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                            const char* text, size_t length, const muiCompositionSegment* segments,
                            uint32_t segmentCount)
{
    muiResult status =
        service == nullptr || blockId.index1 == 0 ? mui_errorInvalid : TextStatus(text, length);
    status = status == mui_success ? SegmentsStatus(segments, segmentCount, length) : status;
    if (status != mui_success)
    {
        return muiCountText(service, status);
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
        return muiCountText(service, mui_errorLimit);
    }
    if (!muiReserve(&service->allocator, &block->segments,
                    segmentCount * sizeof(muiCompositionSegment)) ||
        !ReplaceRange(service, block, start, end, text, (uint32_t)length))
    {
        return mui_errorCapacity;
    }
    MoveSpans(block, start, end, (uint32_t)length);
    if (segmentCount != 0)
    {
        memcpy(block->segments.data, segments, segmentCount * sizeof *segments);
    }
    block->compositionStart = start;
    block->compositionLength = (uint32_t)length;
    block->segmentCount = segmentCount;
    return mui_success;
}

static MUI_NOINLINE muiResult TextBlockSetCompositionWorker(muiTextService* service,
                                                            muiTextBlockId blockId, uint32_t offset,
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
                                      uint32_t offset, const char* text, size_t length,
                                      const muiCompositionSegment* segments, uint32_t segmentCount)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockSetCompositionWorker(service, blockId, offset, text, length,
                                                     segments, segmentCount);
    muiFloatEnvLeave(floatEnv);
    return result;
}

bool muiIsCharacterStart(const muiTextBlock* block, uint32_t at)
{
    const unsigned char* text = block->text.data;
    if (at >= block->length || (text[at] & 0xC0u) != 0x80u)
    {
        return at <= block->length;
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
    (void)muniDecodeUtf8((const char*)text + lead, block->length - lead, &point, &size);
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
            !muiIsCharacterStart(block, span->start) ||
            !muiIsCharacterStart(block, span->start + span->length) ||
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
                                      uint32_t* startOut, uint32_t* lengthOut)
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

uint64_t muiTextService_GetFailures(const muiTextService* service)
{
    return service != nullptr ? service->failures : 0;
}
