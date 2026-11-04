// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A text block's analysis, found apart from the block: a whole text's
// when it is set, and the paragraphs an edit reaches spliced into the
// block's tables when part of it is replaced.

#include "text_analysis.h"

#include "invariant.h"
#include "text_block.h"

#include "maul-ui/base.h"
#include "maul-unicode/script.h"
#include "maul-unicode/segment.h"

#include <stdint.h>
#include <string.h>

// Room Maul Unicode's answers for one paragraph take, kept across the
// paragraphs of an analysis.
typedef struct Scratch
{
    muiBuffer offsets;
    muiBuffer mandatory;
    muiBuffer runs;
} Scratch;

void muiFreeTextAnalysis(const muiAllocator* allocator, muiTextAnalysis* analysis)
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
                       uint32_t at, uint32_t length, muiTextAnalysis* analysis)
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
                        uint32_t at, uint32_t length, muiTextAnalysis* analysis)
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
                              uint32_t end, muiTextAnalysis* analysis)
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
                 muiTextAnalysis* analysis)
{
    uint32_t length = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        // Every text is within MUI_MAX_TEXT_BYTES, a piece's and the one
        // they make (muiBlockTextStatus checked a replacement's), so the
        // sum never wraps.
        MUI_ASSERT(pieces[i].length <= MUI_MAX_TEXT_BYTES - length);
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
    analysis->length = length;
    return true;
}

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
    // Every break before the first at start lies before it.
    uint32_t from = low > 0 ? breaks[low - 1].offset : 0;
    MUI_ASSERT(from <= start);
    return from;
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
                   uint32_t oldTo, int64_t delta, muiTextAnalysis* analysis)
{
    // The region lies within the old text, from its start, so the
    // tables' entries up to from and past oldTo are apart.
    MUI_ASSERT(from <= oldTo && oldTo <= block->length);
    muiTextAnalysis region = *analysis;
    analysis->breaks = (muiBuffer){0};
    analysis->scripts = (muiBuffer){0};
    const muiTextBreak* breaks = block->breaks.data;
    const muiTextScript* scripts = block->scripts.data;
    uint32_t breaksBefore = CountBreaksTo(breaks, block->breakCount, from);
    uint32_t breaksAfter = block->breakCount - CountBreaksTo(breaks, block->breakCount, oldTo);
    uint32_t scriptsBefore = CountScriptsTo(scripts, block->scriptCount, from);
    uint32_t scriptsAfter = block->scriptCount - CountScriptsTo(scripts, block->scriptCount, oldTo);
    MUI_ASSERT(breaksBefore + breaksAfter <= block->breakCount &&
               scriptsBefore + scriptsAfter <= block->scriptCount);
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
            // Past oldTo, so past the region's end once moved, and within
            // the old text, so within the new one.
            MUI_ASSERT(moved.offset > oldTo && moved.offset <= block->length);
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
            MUI_ASSERT(moved.end > oldTo && moved.end <= block->length);
            moved.end = (uint32_t)((int64_t)moved.end + delta);
            outScripts[scriptsBefore + region.scriptCount + i] = moved;
        }
    }
    muiFreeBuffer(allocator, &region.breaks);
    muiFreeBuffer(allocator, &region.scripts);
    return fits;
}

bool muiAnalyzeText(const muiAllocator* allocator, const char* text, uint32_t length,
                    muiTextAnalysis* analysis)
{
    *analysis = (muiTextAnalysis){0};
    const Piece piece = {text, length};
    if (!Join(allocator, &piece, 1, analysis) ||
        !AnalyzeParagraphs(allocator, analysis->text.data, 0, length, analysis))
    {
        muiFreeTextAnalysis(allocator, analysis);
        return false;
    }
    return true;
}

bool muiAnalyzeReplace(const muiAllocator* allocator, const muiTextBlock* block, uint32_t start,
                       uint32_t end, const char* text, uint32_t length, muiTextAnalysis* analysis,
                       uint32_t* fromOut, uint32_t* toOut)
{
    // muiBlockTextStatus held: the range within the text, the result
    // within the limit.
    MUI_ASSERT(start <= end && end <= block->length && length <= MUI_MAX_TEXT_BYTES &&
               block->length - (end - start) <= MUI_MAX_TEXT_BYTES - length);
    *analysis = (muiTextAnalysis){0};
    const char* old = block->text.data;
    const Piece pieces[3] = {{old, start}, {text, length}, {old + end, block->length - end}};
    if (!Join(allocator, pieces, 3, analysis))
    {
        return false;
    }
    uint32_t from = ParagraphBefore(block, start);
    uint32_t to = muiParagraphEnd(analysis->text.data, analysis->length, start + length);
    int64_t delta = (int64_t)analysis->length - (int64_t)block->length;
    if (!AnalyzeParagraphs(allocator, analysis->text.data, from, to, analysis) ||
        !Splice(allocator, block, from, (uint32_t)((int64_t)to - delta), delta, analysis))
    {
        muiFreeTextAnalysis(allocator, analysis);
        return false;
    }
    *fromOut = from;
    *toOut = to;
    return true;
}
