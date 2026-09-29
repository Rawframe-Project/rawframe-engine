// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Line breaking, UAX #14 rules LB1 to LB31. Classes are resolved by LB1
// in the lookup. Rule LB9 folds combining marks and ZWJ into the class
// before them, so most rules look at the previous class after LB9 and,
// for a few, the one before it; rules of the form "X SP* ×" look at the
// last class other than SP. Other properties (General_Category, East
// Asian width, Extended_Pictographic) are read from the code points
// behind those classes only when a rule needs them.
//
// Five rules look ahead, and hold the boundary until they can decide:
// LB15b and LB19a one code point past a quotation mark, LB15c one past an
// infix separator, LB25 one or two past an opening punctuation after a
// prefix or postfix, and LB28a one past an aksara.

#include "complex.h"
#include "segmenter.h"
#include "tables.h"

#include "maul-unicode/properties.h"

#define BIT(value)    ((uint64_t)1 << (value))
#define DOTTED_CIRCLE 0x25CCu
#define LATIN_A       0x41u

// The start of the text as a class. LB1 resolves XX, so it never occurs.
enum
{
    Sot = muni_lbXx,
};

// The kinds of hold.
enum
{
    HoldFinalQuote = 0, // LB15b: SP × final quotation mark, if a closer follows
    HoldInfix = 1,      // LB15c: SP ÷ IS, if a digit follows
    HoldEastAsian = 2,  // LB19a: East Asian × initial quotation mark
    HoldPrefix = 3,     // LB25: (PR | PO) × OP, if (IS)? NU follows
    HoldAksara = 4,     // LB28a: aksara × aksara, if VF follows
};

// Where an LB25 number stands.
enum
{
    NumberNone = 0,
    NumberDigits = 1, // NU (SY | IS)*
    NumberClosed = 2, // NU (SY | IS)* (CL | CP)
};

enum : uint64_t
{
    Hard = BIT(muni_lbBk) | BIT(muni_lbCr) | BIT(muni_lbLf) | BIT(muni_lbNl),
    Marks = BIT(muni_lbCm) | BIT(muni_lbZwj),
    // The classes rule LB9 does not fold marks into.
    NoBase = Hard | BIT(muni_lbSp) | BIT(muni_lbZw),
    Letters = BIT(muni_lbAl) | BIT(muni_lbHl),
    Hyphens = BIT(muni_lbHy) | BIT(muni_lbHh),
    Affixes = BIT(muni_lbPr) | BIT(muni_lbPo),
    Jamo = BIT(muni_lbJl) | BIT(muni_lbJv) | BIT(muni_lbJt) | BIT(muni_lbH2) | BIT(muni_lbH3),
    Ideographs = BIT(muni_lbId) | BIT(muni_lbEb) | BIT(muni_lbEm),
    // What may precede an LB15a initial quotation mark.
    QuoteOpeners = BIT(Sot) | Hard | BIT(muni_lbOp) | BIT(muni_lbQu) | BIT(muni_lbGl) |
                   BIT(muni_lbSp) | BIT(muni_lbZw),
    // What may follow an LB15b final quotation mark.
    QuoteClosers = Hard | BIT(muni_lbSp) | BIT(muni_lbGl) | BIT(muni_lbWj) | BIT(muni_lbCl) |
                   BIT(muni_lbQu) | BIT(muni_lbCp) | BIT(muni_lbEx) | BIT(muni_lbIs) |
                   BIT(muni_lbSy) | BIT(muni_lbZw),
    // What may precede an LB20a word-initial hyphen.
    WordStarts =
        BIT(Sot) | Hard | BIT(muni_lbSp) | BIT(muni_lbZw) | BIT(muni_lbCb) | BIT(muni_lbGl),
};

static bool In(uint8_t value, uint64_t set)
{
    return ((set >> (value & 63u)) & 1u) != 0;
}

static bool IsEastAsian(uint32_t codePoint)
{
    uint8_t width = muniLookupEastAsianWidth(codePoint);
    return width == muni_eawFullwidth || width == muni_eawWide || width == muni_eawHalfwidth;
}

static uint8_t Category(uint32_t codePoint)
{
    return muniLookupGeneralCategory(codePoint);
}

// AK, AS or U+25CC DOTTED CIRCLE, the bases of LB28a.
static bool IsAksara(uint8_t value, uint32_t codePoint)
{
    return value == muni_lbAk || value == muni_lbAs || codePoint == DOTTED_CIRCLE;
}

// LB1, without a tailoring: AI, SG and XX are AL; SA is CM for marks and
// AL otherwise; CJ is NS.
static uint8_t Lookup(uint32_t codePoint)
{
    uint8_t value = muniLookupLineBreak(codePoint);
    switch (value)
    {
    case muni_lbAi:
    case muni_lbSg:
    case muni_lbXx:
        return muni_lbAl;
    case muni_lbSa:
    {
        uint8_t category = Category(codePoint);
        return category == muni_gcMn || category == muni_gcMc ? muni_lbCm : muni_lbAl;
    }
    case muni_lbCj:
        return muni_lbNs;
    default:
        return value;
    }
}

// LB4 to LB8a: hard breaks, spaces and ZWJ, on the actual classes.
static muniDecision DecideHard(const muniLineRules* state, uint8_t value)
{
    if (state->actual == muni_lbBk)
    {
        return muni_decideMandatory; // LB4
    }
    if (state->actual == muni_lbCr && value == muni_lbLf)
    {
        return muni_decideJoin; // LB5
    }
    if (In(state->actual, BIT(muni_lbCr) | BIT(muni_lbLf) | BIT(muni_lbNl)))
    {
        return muni_decideMandatory; // LB5
    }
    if (In(value, Hard | BIT(muni_lbSp) | BIT(muni_lbZw)))
    {
        return muni_decideJoin; // LB6, LB7
    }
    if (state->lead == muni_lbZw)
    {
        return muni_decideBreak; // LB8
    }
    if (state->actual == muni_lbZwj)
    {
        return muni_decideJoin; // LB8a
    }
    return muni_decideContinue;
}

// LB19 and LB19a: quotation marks.
static muniDecision DecideQuotes(const muniLineRules* state, uint8_t value, uint32_t codePoint)
{
    if (value == muni_lbQu)
    {
        if (Category(codePoint) != muni_gcPi)
        {
            return muni_decideJoin; // LB19
        }
        if (!IsEastAsian(state->previousCodePoint) || state->previous == muni_lbBb)
        {
            return muni_decideJoin; // LB19a, or LB21 whatever follows
        }
        return (muniDecision)(muni_decideHold + HoldEastAsian); // LB19a
    }
    if (state->previous == muni_lbQu)
    {
        if (Category(state->previousCodePoint) != muni_gcPf)
        {
            return muni_decideJoin; // LB19
        }
        if (!IsEastAsian(codePoint) || state->before == Sot || !IsEastAsian(state->beforeCodePoint))
        {
            return muni_decideJoin; // LB19a
        }
    }
    return muni_decideContinue;
}

// LB11 to LB19a: glue, punctuation and spaces.
static muniDecision DecidePunctuation(const muniLineRules* state, uint8_t value, uint32_t codePoint)
{
    uint8_t previous = state->previous;
    if (value == muni_lbWj || previous == muni_lbWj || previous == muni_lbGl)
    {
        return muni_decideJoin; // LB11, LB12
    }
    if (value == muni_lbGl && !In(previous, BIT(muni_lbSp) | Hyphens))
    {
        return muni_decideJoin; // LB12a
    }
    if (In(value, BIT(muni_lbCl) | BIT(muni_lbCp) | BIT(muni_lbEx) | BIT(muni_lbSy)))
    {
        return muni_decideJoin; // LB13
    }
    if (state->lead == muni_lbOp || (state->lead == muni_lbQu && state->leadInitialQuote))
    {
        return muni_decideJoin; // LB14, LB15a
    }
    if (value == muni_lbQu && Category(codePoint) == muni_gcPf)
    {
        // LB15b when a space precedes; otherwise LB19 joins.
        return previous == muni_lbSp ? (muniDecision)(muni_decideHold + HoldFinalQuote)
                                     : muni_decideJoin;
    }
    if (value == muni_lbIs)
    {
        // LB15c when a space precedes; otherwise LB15d joins.
        return previous == muni_lbSp ? (muniDecision)(muni_decideHold + HoldInfix)
                                     : muni_decideJoin;
    }
    if ((In(state->lead, BIT(muni_lbCl) | BIT(muni_lbCp)) && value == muni_lbNs) ||
        (state->lead == muni_lbB2 && value == muni_lbB2))
    {
        return muni_decideJoin; // LB16, LB17
    }
    if (previous == muni_lbSp)
    {
        return muni_decideBreak; // LB18
    }
    return DecideQuotes(state, value, codePoint);
}

// LB25: numbers.
static muniDecision DecideNumber(const muniLineRules* state, uint8_t value)
{
    if (state->number == NumberClosed && In(value, Affixes))
    {
        return muni_decideJoin;
    }
    if (state->number == NumberDigits && In(value, Affixes | BIT(muni_lbNu)))
    {
        return muni_decideJoin;
    }
    if (In(state->previous, Affixes) && value == muni_lbOp)
    {
        return (muniDecision)(muni_decideHold + HoldPrefix);
    }
    if (In(state->previous, Affixes | BIT(muni_lbHy) | BIT(muni_lbIs)) && value == muni_lbNu)
    {
        return muni_decideJoin;
    }
    return muni_decideContinue;
}

// LB20 to LB25: contingent breaks, hyphens, affixes and numbers.
static muniDecision DecideWords(const muniLineRules* state, uint8_t value)
{
    uint8_t previous = state->previous;
    if (value == muni_lbCb || previous == muni_lbCb)
    {
        return muni_decideBreak; // LB20
    }
    if (In(previous, Hyphens) && In(state->before, WordStarts) && In(value, Letters))
    {
        return muni_decideJoin; // LB20a
    }
    if (In(value, BIT(muni_lbBa) | Hyphens | BIT(muni_lbNs)) || previous == muni_lbBb)
    {
        return muni_decideJoin; // LB21
    }
    if ((state->before == muni_lbHl && In(previous, Hyphens) && value != muni_lbHl) ||
        (previous == muni_lbSy && value == muni_lbHl) || value == muni_lbIn)
    {
        return muni_decideJoin; // LB21a, LB21b, LB22
    }
    if ((In(previous, Letters) && value == muni_lbNu) ||
        (previous == muni_lbNu && In(value, Letters)))
    {
        return muni_decideJoin; // LB23
    }
    if ((previous == muni_lbPr && In(value, Ideographs)) ||
        (In(previous, Ideographs) && value == muni_lbPo))
    {
        return muni_decideJoin; // LB23a
    }
    if ((In(previous, Affixes) && In(value, Letters)) ||
        (In(previous, Letters) && In(value, Affixes)))
    {
        return muni_decideJoin; // LB24
    }
    return DecideNumber(state, value);
}

// LB26 to LB28a: Korean syllables, letters and Brahmic syllables.
static muniDecision DecideSyllables(const muniLineRules* state, uint8_t value, uint32_t codePoint)
{
    uint8_t previous = state->previous;
    if ((previous == muni_lbJl &&
         In(value, BIT(muni_lbJl) | BIT(muni_lbJv) | BIT(muni_lbH2) | BIT(muni_lbH3))) ||
        (In(previous, BIT(muni_lbJv) | BIT(muni_lbH2)) &&
         In(value, BIT(muni_lbJv) | BIT(muni_lbJt))) ||
        (In(previous, BIT(muni_lbJt) | BIT(muni_lbH3)) && value == muni_lbJt))
    {
        return muni_decideJoin; // LB26
    }
    if ((In(previous, Jamo) && value == muni_lbPo) || (previous == muni_lbPr && In(value, Jamo)) ||
        (In(previous, Letters) && In(value, Letters)))
    {
        return muni_decideJoin; // LB27, LB28
    }
    bool aksara = IsAksara(previous, state->previousCodePoint);
    if ((previous == muni_lbAp && IsAksara(value, codePoint)) ||
        (aksara && In(value, BIT(muni_lbVf) | BIT(muni_lbVi))) ||
        (IsAksara(state->before, state->beforeCodePoint) && previous == muni_lbVi &&
         (value == muni_lbAk || codePoint == DOTTED_CIRCLE)))
    {
        return muni_decideJoin; // LB28a
    }
    if (aksara && IsAksara(value, codePoint))
    {
        return (muniDecision)(muni_decideHold + HoldAksara); // LB28a
    }
    return muni_decideContinue;
}

// LB29 to LB31.
static muniDecision DecideRest(const muniLineRules* state, uint8_t value, uint32_t codePoint)
{
    uint8_t previous = state->previous;
    if (previous == muni_lbIs && In(value, Letters))
    {
        return muni_decideJoin; // LB29
    }
    if ((In(previous, Letters | BIT(muni_lbNu)) && value == muni_lbOp && !IsEastAsian(codePoint)) ||
        (previous == muni_lbCp && !IsEastAsian(state->previousCodePoint) &&
         In(value, Letters | BIT(muni_lbNu))))
    {
        return muni_decideJoin; // LB30
    }
    if (previous == muni_lbRi && value == muni_lbRi && state->oddRegional)
    {
        return muni_decideJoin; // LB30a
    }
    if (value == muni_lbEm &&
        (previous == muni_lbEb ||
         (Category(state->previousCodePoint) == muni_gcCn &&
          (muniLookupEmoji(state->previousCodePoint) & muni_emojiExtendedPictographic) != 0)))
    {
        return muni_decideJoin; // LB30b
    }
    return muni_decideBreak; // LB31
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    const muniLineRules* state = &segmenter->rules.line;
    muniDecision decision = DecideHard(state, value);
    if (decision != muni_decideContinue)
    {
        return decision;
    }
    if (muniComplexBreakAt(segmenter) == muni_complexBreak && !In(value, Marks))
    {
        return muni_decideBreak; // a break the caller's SA segmenter found
    }
    if (In(value, Marks))
    {
        if (!In(state->previous, NoBase))
        {
            return muni_decideJoin; // LB9
        }
        value = muni_lbAl; // LB10
        codePoint = LATIN_A;
    }
    decision = DecidePunctuation(state, value, codePoint);
    if (decision == muni_decideContinue)
    {
        decision = DecideWords(state, value);
    }
    if (decision == muni_decideContinue)
    {
        decision = DecideSyllables(state, value, codePoint);
    }
    return decision == muni_decideContinue ? DecideRest(state, value, codePoint) : decision;
}

// While a hold is open: marks fold into the held code point (LB9), and
// the next code point settles the rule that held.
static muniDecision Resolve(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    bool end = value == muni_segmentEnd;
    if (!end && In(value, Marks))
    {
        return muni_decideContinue;
    }
    switch (segmenter->holdKind)
    {
    case HoldFinalQuote:
        return end || In(value, QuoteClosers) ? muni_decideJoin : muni_decideBreak;
    case HoldInfix:
        return value == muni_lbNu ? muni_decideBreak : muni_decideJoin;
    case HoldEastAsian:
        return end || !IsEastAsian(codePoint) ? muni_decideJoin : muni_decideBreak;
    case HoldPrefix:
        if (value == muni_lbIs && !segmenter->rules.line.sawInfix)
        {
            return muni_decideContinue;
        }
        return value == muni_lbNu ? muni_decideJoin : muni_decideBreak;
    default:
        return value == muni_lbVf ? muni_decideJoin : muni_decideBreak;
    }
}

static uint8_t NextNumber(uint8_t number, uint8_t value)
{
    if (value == muni_lbNu)
    {
        return NumberDigits;
    }
    if (number == NumberDigits && In(value, BIT(muni_lbSy) | BIT(muni_lbIs)))
    {
        return NumberDigits;
    }
    if (number == NumberDigits && In(value, BIT(muni_lbCl) | BIT(muni_lbCp)))
    {
        return NumberClosed;
    }
    return NumberNone;
}

static void Absorb(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    muniLineRules* state = &segmenter->rules.line;
    muniComplexTrack(segmenter, codePoint);
    bool folded = In(value, Marks) && segmenter->started && !In(state->previous, NoBase);
    state->actual = value;
    if (folded)
    {
        return; // LB9
    }
    if (In(value, Marks))
    {
        value = muni_lbAl; // LB10
        codePoint = LATIN_A;
    }
    state->number = NextNumber(state->number, value);
    if (value != muni_lbSp)
    {
        state->leadInitialQuote = value == muni_lbQu && Category(codePoint) == muni_gcPi &&
                                  In(state->previous, QuoteOpeners);
        state->lead = value;
    }
    if (segmenter->holding && segmenter->holdKind == HoldPrefix)
    {
        state->sawInfix = value == muni_lbIs;
    }
    state->oddRegional = value == muni_lbRi && !state->oddRegional;
    state->before = state->previous;
    state->beforeCodePoint = state->previousCodePoint;
    state->previous = value;
    state->previousCodePoint = codePoint;
}

muniResult muniNextLineSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Resolve, Absorb);
}
