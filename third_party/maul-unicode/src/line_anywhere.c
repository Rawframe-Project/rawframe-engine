// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Line breaking at the anywhere level of CSS's line-break property: a
// break at every grapheme cluster boundary, but the hard breaks of LB4
// and LB5 stay mandatory and LB6 keeps a line from ending just before
// one. It has a file of its own so that the default rules in line.c keep
// one rule loop, which the compiler inlines.

#include "grapheme.h"
#include "segmenter.h"
#include "tables.h"

#include "maul-unicode/properties.h"

#define BIT(value) ((uint32_t)1 << (value))

enum : uint32_t
{
    Hard = BIT(muni_lbBk) | BIT(muni_lbCr) | BIT(muni_lbLf) | BIT(muni_lbNl),
};

static bool In(uint8_t value, uint32_t set)
{
    return value < 32 && ((set >> value) & 1u) != 0;
}

// The Line_Break class, which LB1 does not change for the hard breaks,
// the only classes these rules read.
static uint8_t Lookup(const muniSegmenter* segmenter, uint32_t codePoint)
{
    (void)segmenter;
    return muniLookupLineBreak(codePoint);
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    const muniLineRules* state = &segmenter->rules.line;
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
    if (In(value, Hard))
    {
        return muni_decideJoin; // LB6
    }
    uint8_t cluster = muniLookupGraphemeClusterBreak(codePoint);
    return muniGraphemeIsBoundary(&state->grapheme, cluster, codePoint) ? muni_decideBreak
                                                                        : muni_decideJoin;
}

// Keeps only the class for LB4 to LB6 and the grapheme cluster rules.
static void Absorb(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    muniLineRules* state = &segmenter->rules.line;
    state->actual = value;
    muniGraphemeAdvance(&state->grapheme, muniLookupGraphemeClusterBreak(codePoint), codePoint);
}

muniResult muniNextLineAnywhereSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    // Decide never holds, so Decide stands in for the resolve rule too.
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Decide, Absorb, nullptr);
}
