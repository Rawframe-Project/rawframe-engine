// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The engine the three UAX #29 segmenters share. It walks the text with a
// cursor and asks a rule set, for each code point, whether there is a
// boundary before it. A rule that needs to see further text answers
// "hold": the engine keeps the undecided boundary, reads on, and asks the
// rule set about each following code point until the hold resolves.
//
// The engine is a static inline function taking the rules as function
// arguments; each rule set calls it with its own functions, so the
// compiler builds one loop per rule set with the rules inlined.

#ifndef MAUL_UNICODE_SRC_SEGMENTER_H
#define MAUL_UNICODE_SRC_SEGMENTER_H

#include "cursor.h"
#include "invariant.h"

#include "maul-unicode/segment.h"

// Which segmentation an iterator performs.
enum
{
    muni_segmentGrapheme = 1,
    muni_segmentWord = 2,
    muni_segmentSentence = 3,
    muni_segmentLine = 4,
};

// What an iterator in caller memory is doing between calls, and during a
// call that hands a run to the caller's word segmenter.
enum
{
    muni_phaseReady = 0,   // it can be asked for a boundary
    muni_phaseWaiting = 1, // the last call returned muni_needMoreText
    muni_phaseBusy = 2,    // a call is inside the word segmenter
};

// A rule set's answer about the position before a code point. A hold
// may carry a kind, muni_decideHold plus a small number, which the engine
// keeps in holdKind for the resolve rule.
typedef uint8_t muniDecision;

enum
{
    muni_decideBreak = 0,     // a boundary
    muni_decideMandatory = 1, // a boundary that must be taken (line breaking)
    muni_decideJoin = 2,      // no boundary
    muni_decideContinue = 3,  // while holding: still undecided, read on
    muni_decideHold = 4,      // undecided until later text is seen
};

// The value the resolve rule sees at the end of the text.
enum
{
    muni_segmentEnd = 0xFF,
};

// The state of each rule set, beyond the previous code point.
typedef struct muniGraphemeRules
{
    uint8_t previous; // the previous code point's Grapheme_Cluster_Break
    uint8_t emoji;    // where a GB11 emoji sequence stands
    bool linker;      // InCB=Linker followed by InCB=Extend only, for GB9c
    bool oddRegional; // an odd number of regional indicators precedes
} muniGraphemeRules;

typedef struct muniWordRules
{
    uint8_t actual;         // the previous code point's Word_Break
    uint8_t previous;       // the previous one after rule WB4, which skips Extend
    uint8_t before;         // the one before that, after rule WB4
    bool oddRegional;       // an odd number of regional indicators precedes
    uint32_t overrideCount; // the entries of the segmenter's overrides
} muniWordRules;

typedef struct muniSentenceRules
{
    uint8_t actual;         // the previous code point's Sentence_Break
    uint8_t previous;       // the previous one after rule SB5, which skips Extend
    uint8_t before;         // the one before that, after rule SB5
    uint8_t ending;         // where a sentence ending "SATerm Close* Sp*" stands
    bool aterm;             // the ending's terminator is an ATerm
    uint32_t overrideCount; // the entries of the segmenter's overrides
} muniSentenceRules;

typedef struct muniLineRules
{
    uint32_t previousCodePoint; // the code point behind previous
    uint32_t beforeCodePoint;   // the code point behind before
    uint8_t actual;             // the previous code point's class after LB1
    uint8_t previous;           // the previous class after LB9 and LB10
    uint8_t before;             // the one before that
    uint8_t lead;               // the last class other than SP
    uint8_t number;             // where an LB25 number stands
    bool leadInitialQuote;      // lead is an LB15a initial quotation mark
    bool sawInfix;              // an LB25 hold has read "OP IS"
    bool oddRegional;           // an odd number of regional indicators precedes
} muniLineRules;

// A run of Thai, Lao, Khmer or Burmese (Line_Break=SA) that a caller's
// breaker segments. The run lies in the current piece.
typedef struct muniComplexRun
{
    muniComplexBreakFn breaker; // NULL when the caller set none
    void* context;              // the breaker's context
    const uint8_t* text;        // the run, or NULL outside a run
    size_t start;               // its offset in the whole text
    size_t length;              // its length in bytes
    size_t next;                // the next break in it, relative to start
} muniComplexRun;

typedef struct muniSegmenter
{
    muniCursor cursor;
    muniComplexRun complex;
    size_t heldOffset; // the offset of the undecided boundary
    uint8_t kind;      // a muni_segment value
    bool started;      // a code point has been read
    bool endReported;  // the boundary at the end has been reported
    bool holding;      // a boundary at heldOffset is undecided
    uint8_t phase;     // a muni_phase value
    bool mandatory;    // the last boundary reported must be taken
    uint8_t holdKind;  // the kind of the open hold
    uint8_t options;   // a line iterator's muni_line options

    union
    {
        muniGraphemeRules grapheme;
        muniWordRules word;
        muniSentenceRules sentence;
        muniLineRules line;
    } rules;

    // The caller's class overrides of a word or sentence iterator, sorted
    // by code point; their count is in the word or sentence rules.
    const muniBreakClassOverride* overrides;
} muniSegmenter;

static_assert(sizeof(muniSegmenter) <= sizeof(muniSegmentIterator),
              "the public iterator must hold the segmenter");

// The rules as functions of the segmenter and the next code point: its
// property value from lookup, the decision about the boundary before it,
// the decision while a hold is open, and the update after it is read.
typedef uint8_t (*muniLookupRule)(const muniSegmenter* segmenter, uint32_t codePoint);
typedef muniDecision (*muniDecideRule)(const muniSegmenter* segmenter, uint8_t value,
                                       uint32_t codePoint);
typedef void (*muniAbsorbRule)(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint);

// The class overrides give a code point, or value when they do not name
// it: a binary search, so a long table costs a logarithm per code point.
static inline uint8_t muniOverrideClass(const muniBreakClassOverride* overrides, uint32_t count,
                                        uint32_t codePoint, uint8_t value)
{
    // Set only with a table of at least one entry.
    MUNI_ASSERT(overrides != nullptr && count > 0);
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        if (overrides[middle].codePoint < codePoint)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < count && overrides[low].codePoint == codePoint ? overrides[low].value : value;
}

// Reports a boundary.
static inline muniResult muniSegmenterReport(muniSegmenter* segmenter, size_t offset,
                                             bool mandatory, size_t* offsetOut)
{
    // Every boundary lies after at least one code point.
    MUNI_ASSERT(offset > 0);
    segmenter->mandatory = mandatory;
    *offsetOut = offset;
    return muni_success;
}

// The end of the text: a hold resolves with muni_segmentEnd, then the end
// itself is a boundary unless the text is empty.
static inline muniResult muniSegmenterEnd(muniSegmenter* segmenter, size_t* offsetOut,
                                          muniDecideRule resolve)
{
    if (segmenter->holding)
    {
        segmenter->holding = false;
        if (resolve(segmenter, muni_segmentEnd, 0) != muni_decideJoin)
        {
            return muniSegmenterReport(segmenter, segmenter->heldOffset, false, offsetOut);
        }
    }
    if (!segmenter->started || segmenter->endReported)
    {
        return muni_done;
    }
    segmenter->endReported = true;
    return muniSegmenterReport(segmenter, segmenter->cursor.offset, true, offsetOut);
}

// Finds the next boundary: muni_success and its offset, muni_done after
// the end, or muni_needMoreText.
static inline muniResult muniSegmenterNext(muniSegmenter* segmenter, size_t* offsetOut,
                                           muniLookupRule lookup, muniDecideRule decide,
                                           muniDecideRule resolve, muniAbsorbRule absorb)
{
    for (;;)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniCursorPeek(&segmenter->cursor, &codePoint, &size);
        if (status == muni_needMoreText)
        {
            return status;
        }
        if (status == muni_done)
        {
            return muniSegmenterEnd(segmenter, offsetOut, resolve);
        }
        uint8_t value = lookup(segmenter, codePoint);
        muniDecision decision = muni_decideJoin;
        if (segmenter->holding)
        {
            MUNI_ASSERT(segmenter->heldOffset < segmenter->cursor.offset);
            decision = resolve(segmenter, value, codePoint);
            if (decision == muni_decideBreak)
            {
                // The hold failed: report it, and decide about this code
                // point on the next call.
                segmenter->holding = false;
                return muniSegmenterReport(segmenter, segmenter->heldOffset, false, offsetOut);
            }
            segmenter->holding = decision == muni_decideContinue;
        }
        if (!segmenter->holding && segmenter->started)
        {
            decision = decide(segmenter, value, codePoint);
        }
        size_t offset = segmenter->cursor.offset;
        if (decision >= muni_decideHold)
        {
            segmenter->holding = true;
            segmenter->holdKind = (uint8_t)(decision - muni_decideHold);
            segmenter->heldOffset = offset;
        }
        absorb(segmenter, value, codePoint);
        segmenter->started = true;
        muniCursorAdvance(&segmenter->cursor, size);
        if (decision <= muni_decideMandatory)
        {
            return muniSegmenterReport(segmenter, offset, decision == muni_decideMandatory,
                                       offsetOut);
        }
    }
}

// The engine's entry for each rule set, defined in grapheme.c, word.c,
// sentence.c and line.c.
muniResult muniNextGraphemeSegment(muniSegmenter* segmenter, size_t* offsetOut);
muniResult muniNextWordSegment(muniSegmenter* segmenter, size_t* offsetOut);
muniResult muniNextSentenceSegment(muniSegmenter* segmenter, size_t* offsetOut);
muniResult muniNextLineSegment(muniSegmenter* segmenter, size_t* offsetOut);

#endif // MAUL_UNICODE_SRC_SEGMENTER_H
