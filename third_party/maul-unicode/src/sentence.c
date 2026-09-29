// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sentence boundaries, UAX #29 rules SB3 to SB998. Rule SB5 makes Extend
// and Format transparent. A sentence can end after "SATerm Close* Sp*";
// the state tracks how far into such an ending the text is. Rule SB8
// looks ahead without limit: after "ATerm Close* Sp*" there is no
// boundary when a lowercase letter follows before any letter, separator
// or terminator. The engine holds the boundary while it reads on.

#include "segmenter.h"
#include "tables.h"

#include "maul-unicode/properties.h"

#define BIT(value) (1u << (value))

// Where a sentence ending stands.
enum
{
    EndingNone = 0,
    EndingTerm = 1,  // SATerm Close*
    EndingSpace = 2, // SATerm Close* Sp+
};

enum
{
    ParaSep = BIT(muni_sbSep) | BIT(muni_sbCr) | BIT(muni_sbLf),
    Ignored = BIT(muni_sbExtend) | BIT(muni_sbFormat),
    SATerm = BIT(muni_sbSTerm) | BIT(muni_sbATerm),
    UpperLower = BIT(muni_sbUpper) | BIT(muni_sbLower),
    // What stops the SB8 lookahead, Lower aside.
    Stops = BIT(muni_sbOLetter) | BIT(muni_sbUpper) | ParaSep | SATerm,
};

static bool In(uint8_t value, uint32_t set)
{
    return ((set >> (value & 31u)) & 1u) != 0;
}

// SB8 to SB11: after "SATerm Close* Sp*".
static muniDecision DecideEnding(const muniSentenceRules* state, uint8_t value)
{
    if (In(value, BIT(muni_sbSContinue) | SATerm))
    {
        return muni_decideJoin; // SB8a
    }
    if (state->ending == EndingTerm && In(value, BIT(muni_sbClose) | BIT(muni_sbSp) | ParaSep))
    {
        return muni_decideJoin; // SB9
    }
    if (In(value, BIT(muni_sbSp) | ParaSep))
    {
        return muni_decideJoin; // SB10
    }
    if (state->aterm && value == muni_sbLower)
    {
        return muni_decideJoin; // SB8
    }
    if (state->aterm && !In(value, Stops))
    {
        return muni_decideHold; // SB8, until a Lower or a stop
    }
    return muni_decideBreak; // SB11
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    (void)codePoint;
    const muniSentenceRules* state = &segmenter->rules.sentence;
    if (state->actual == muni_sbCr && value == muni_sbLf)
    {
        return muni_decideJoin; // SB3
    }
    if (In(state->actual, ParaSep))
    {
        return muni_decideBreak; // SB4
    }
    if (In(value, Ignored))
    {
        return muni_decideJoin; // SB5
    }
    if (state->previous == muni_sbATerm && value == muni_sbNumeric)
    {
        return muni_decideJoin; // SB6
    }
    if (In(state->before, UpperLower) && state->previous == muni_sbATerm && value == muni_sbUpper)
    {
        return muni_decideJoin; // SB7
    }
    if (state->ending != EndingNone)
    {
        return DecideEnding(state, value);
    }
    return muni_decideJoin; // SB998
}

// While SB8 holds: a Lower keeps the sentence going, a stop ends it at
// the held boundary, and anything else is read past.
static muniDecision Resolve(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    (void)segmenter;
    (void)codePoint;
    if (value == muni_segmentEnd)
    {
        return muni_decideBreak;
    }
    if (value == muni_sbLower)
    {
        return muni_decideJoin;
    }
    return In(value, Stops) ? muni_decideBreak : muni_decideContinue;
}

static uint8_t NextEnding(const muniSentenceRules* state, uint8_t value)
{
    if (In(value, SATerm))
    {
        return EndingTerm;
    }
    if (value == muni_sbClose && state->ending == EndingTerm)
    {
        return EndingTerm;
    }
    if (value == muni_sbSp && state->ending != EndingNone)
    {
        return EndingSpace;
    }
    return EndingNone;
}

static void Absorb(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    (void)codePoint;
    muniSentenceRules* state = &segmenter->rules.sentence;
    // SB5 skips Extend and Format, except at the start and after a
    // paragraph separator, where they count as themselves.
    bool skipped = In(value, Ignored) && segmenter->started && !In(state->actual, ParaSep);
    if (!skipped)
    {
        state->ending = NextEnding(state, value);
        if (In(value, SATerm))
        {
            state->aterm = value == muni_sbATerm;
        }
        state->before = state->previous;
        state->previous = value;
    }
    state->actual = value;
}

static uint8_t Lookup(uint32_t codePoint)
{
    return muniLookupSentenceBreak(codePoint);
}

muniResult muniNextSentenceSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Resolve, Absorb);
}
