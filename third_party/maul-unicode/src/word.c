// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Word boundaries, UAX #29 rules WB3 to WB999. Rule WB4 makes Extend,
// Format and ZWJ transparent, so the letter and number rules look at the
// previous two code points that are not those. Three rules look one code
// point ahead: WB6 (a letter, then MidLetter, keeps together only when a
// letter follows), WB7b (the same for a Hebrew letter and a double quote)
// and WB12 (the same for numbers). Those hold the boundary before the
// middle code point until the next code point after rule WB4 is seen.

#include "complex.h"
#include "segmenter.h"
#include "tables.h"

#include "maul-unicode/properties.h"

#define BIT(value) (1u << (value))

enum
{
    Newlines = BIT(muni_wbNewline) | BIT(muni_wbCr) | BIT(muni_wbLf),
    Ignored = BIT(muni_wbExtend) | BIT(muni_wbFormat) | BIT(muni_wbZwj),
    AHLetter = BIT(muni_wbALetter) | BIT(muni_wbHebrewLetter),
    MidNumLetQ = BIT(muni_wbMidNumLet) | BIT(muni_wbSingleQuote),
    MidLetterQ = BIT(muni_wbMidLetter) | MidNumLetQ,
    MidNumQ = BIT(muni_wbMidNum) | MidNumLetQ,
    Numeric = BIT(muni_wbNumeric),
    Katakana = BIT(muni_wbKatakana),
    ExtendNumLet = BIT(muni_wbExtendNumLet),
    Hebrew = BIT(muni_wbHebrewLetter),
};

static bool In(uint8_t value, uint32_t set)
{
    return ((set >> (value & 31u)) & 1u) != 0;
}

// WB3 to WB4: the rules about the actual previous code point.
static muniDecision DecideAdjacent(const muniWordRules* state, uint8_t value, uint32_t codePoint)
{
    if (state->actual == muni_wbCr && value == muni_wbLf)
    {
        return muni_decideJoin; // WB3
    }
    if (In(state->actual, Newlines) || In(value, Newlines))
    {
        return muni_decideBreak; // WB3a, WB3b
    }
    if (state->actual == muni_wbZwj &&
        (muniLookupEmoji(codePoint) & muni_emojiExtendedPictographic) != 0)
    {
        return muni_decideJoin; // WB3c
    }
    if (state->actual == muni_wbWSegSpace && value == muni_wbWSegSpace)
    {
        return muni_decideJoin; // WB3d
    }
    if (In(value, Ignored))
    {
        return muni_decideJoin; // WB4
    }
    return muni_decideContinue;
}

// WB5 to WB7c: letters.
static muniDecision DecideLetters(const muniWordRules* state, uint8_t value)
{
    uint8_t previous = state->previous;
    if (In(previous, AHLetter) && In(value, AHLetter))
    {
        return muni_decideJoin; // WB5
    }
    if (previous == muni_wbHebrewLetter && value == muni_wbSingleQuote)
    {
        return muni_decideJoin; // WB7a, which also ends WB6 when it fails
    }
    if (In(previous, AHLetter) && In(value, MidLetterQ))
    {
        return muni_decideHold; // WB6
    }
    if (In(state->before, AHLetter) && In(previous, MidLetterQ) && In(value, AHLetter))
    {
        return muni_decideJoin; // WB7
    }
    if (previous == muni_wbHebrewLetter && value == muni_wbDoubleQuote)
    {
        return muni_decideHold; // WB7b
    }
    if (state->before == muni_wbHebrewLetter && previous == muni_wbDoubleQuote &&
        value == muni_wbHebrewLetter)
    {
        return muni_decideJoin; // WB7c
    }
    return muni_decideContinue;
}

// WB8 to WB13b: numbers, katakana and connectors.
static muniDecision DecideNumbers(const muniWordRules* state, uint8_t value)
{
    uint8_t previous = state->previous;
    if (In(previous, AHLetter | Numeric) && In(value, AHLetter | Numeric))
    {
        return muni_decideJoin; // WB8, WB9, WB10 (WB5 took letter pairs)
    }
    if (state->before == muni_wbNumeric && In(previous, MidNumQ) && value == muni_wbNumeric)
    {
        return muni_decideJoin; // WB11
    }
    if (previous == muni_wbNumeric && In(value, MidNumQ))
    {
        return muni_decideHold; // WB12
    }
    if (previous == muni_wbKatakana && value == muni_wbKatakana)
    {
        return muni_decideJoin; // WB13
    }
    if (In(previous, AHLetter | Numeric | Katakana | ExtendNumLet) && value == muni_wbExtendNumLet)
    {
        return muni_decideJoin; // WB13a
    }
    if (previous == muni_wbExtendNumLet && In(value, AHLetter | Numeric | Katakana))
    {
        return muni_decideJoin; // WB13b
    }
    return muni_decideContinue;
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    const muniWordRules* state = &segmenter->rules.word;
    muniComplexAnswer complex = muniComplexBreakAt(segmenter);
    if (complex != muni_complexNone)
    {
        // Inside an SA run the caller's segmenter replaces the rules,
        // except that WB4 keeps marks with their letters.
        return complex == muni_complexBreak && !In(value, Ignored) ? muni_decideBreak
                                                                   : muni_decideJoin;
    }
    muniDecision decision = DecideAdjacent(state, value, codePoint);
    if (decision == muni_decideContinue)
    {
        decision = DecideLetters(state, value);
    }
    if (decision == muni_decideContinue)
    {
        decision = DecideNumbers(state, value);
    }
    if (decision != muni_decideContinue)
    {
        return decision;
    }
    if (state->previous == muni_wbRegionalIndicator && value == muni_wbRegionalIndicator &&
        state->oddRegional)
    {
        return muni_decideJoin; // WB15, WB16
    }
    return muni_decideBreak; // WB999
}

// While a hold is open, previous is the middle code point and before is
// what precedes it. The hold resolves at the next code point after WB4.
static muniDecision Resolve(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    (void)codePoint;
    const muniWordRules* state = &segmenter->rules.word;
    if (value == muni_segmentEnd)
    {
        return muni_decideBreak;
    }
    if (In(value, Ignored))
    {
        return muni_decideContinue; // WB4
    }
    uint32_t need = state->before == muni_wbNumeric         ? Numeric
                    : state->previous == muni_wbDoubleQuote ? Hebrew
                                                            : AHLetter;
    return In(value, need) ? muni_decideJoin : muni_decideBreak;
}

static void Absorb(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    muniWordRules* state = &segmenter->rules.word;
    muniComplexTrack(segmenter, codePoint);
    // WB4 skips Extend, Format and ZWJ, except at the start and after a
    // newline, where they count as themselves.
    bool skipped = In(value, Ignored) && segmenter->started && !In(state->actual, Newlines);
    if (!skipped)
    {
        state->oddRegional = value == muni_wbRegionalIndicator && !state->oddRegional;
        state->before = state->previous;
        state->previous = value;
    }
    state->actual = value;
}

static uint8_t Lookup(uint32_t codePoint)
{
    return muniLookupWordBreak(codePoint);
}

muniResult muniNextWordSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Resolve, Absorb);
}
