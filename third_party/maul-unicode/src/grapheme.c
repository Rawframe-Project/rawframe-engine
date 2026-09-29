// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Extended grapheme cluster boundaries, UAX #29 rules GB3 to GB999. The
// rules look back, never ahead, so a boundary before a code point is
// decided from that code point and the state the iterator carries:
//
// - the previous code point's Grapheme_Cluster_Break;
// - whether the text since the last InCB=Linker is Linker Extend*, for GB9c;
// - whether it is Extended_Pictographic Extend* (and then ZWJ), for GB11;
// - whether an odd number of regional indicators precedes, for GB12 and
//   GB13.

#include "segmenter.h"
#include "tables.h"

#include "maul-unicode/properties.h"

// Where a GB11 emoji sequence stands.
enum
{
    EmojiNone = 0,
    EmojiPictographic = 1, // Extended_Pictographic Extend*
    EmojiJoined = 2,       // Extended_Pictographic Extend* ZWJ
};

static bool IsControl(uint8_t value)
{
    return value == muni_gcbControl || value == muni_gcbCr || value == muni_gcbLf;
}

// GB6 to GB8: Hangul syllable sequences stay together.
static bool JoinsHangul(uint8_t before, uint8_t after)
{
    if (before == muni_gcbL)
    {
        return after == muni_gcbL || after == muni_gcbV || after == muni_gcbLv ||
               after == muni_gcbLvt;
    }
    if (before == muni_gcbLv || before == muni_gcbV)
    {
        return after == muni_gcbV || after == muni_gcbT;
    }
    return (before == muni_gcbLvt || before == muni_gcbT) && after == muni_gcbT;
}

// Whether there is a boundary between the text so far and a code point
// whose Grapheme_Cluster_Break is after. No rule looks ahead.
static bool IsBoundary(const muniGraphemeRules* state, uint8_t after, uint32_t codePoint)
{
    uint8_t before = state->previous;
    if (before == muni_gcbCr && after == muni_gcbLf)
    {
        return false; // GB3
    }
    if (IsControl(before) || IsControl(after))
    {
        return true; // GB4, GB5
    }
    if (JoinsHangul(before, after))
    {
        return false; // GB6 to GB8
    }
    if (after == muni_gcbExtend || after == muni_gcbZwj || after == muni_gcbSpacingMark ||
        before == muni_gcbPrepend)
    {
        return false; // GB9, GB9a, GB9b
    }
    if (state->linker && muniLookupIndicConjunctBreak(codePoint) == muni_incbConsonant)
    {
        return false; // GB9c
    }
    if (state->emoji == EmojiJoined && before == muni_gcbZwj &&
        (muniLookupEmoji(codePoint) & muni_emojiExtendedPictographic) != 0)
    {
        return false; // GB11
    }
    if (before == muni_gcbRegionalIndicator && after == muni_gcbRegionalIndicator &&
        state->oddRegional)
    {
        return false; // GB12, GB13
    }
    return true; // GB999
}

// Moves the state past a code point whose Grapheme_Cluster_Break is value.
static void Absorb(muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    muniGraphemeRules* state = &segmenter->rules.grapheme;
    if (codePoint < 0x80)
    {
        // ASCII has no InCB value and nothing Extended_Pictographic.
        state->linker = false;
        state->emoji = EmojiNone;
        state->oddRegional = false;
        state->previous = value;
        return;
    }
    uint8_t conjunct = muniLookupIndicConjunctBreak(codePoint);
    if (conjunct == muni_incbLinker)
    {
        state->linker = true;
    }
    else if (conjunct != muni_incbExtend)
    {
        state->linker = false;
    }
    if ((muniLookupEmoji(codePoint) & muni_emojiExtendedPictographic) != 0)
    {
        state->emoji = EmojiPictographic;
    }
    else if (value == muni_gcbExtend && state->emoji == EmojiPictographic)
    {
        state->emoji = EmojiPictographic;
    }
    else if (value == muni_gcbZwj && state->emoji == EmojiPictographic)
    {
        state->emoji = EmojiJoined;
    }
    else
    {
        state->emoji = EmojiNone;
    }
    state->oddRegional = value == muni_gcbRegionalIndicator && !state->oddRegional;
    state->previous = value;
}

static muniDecision Decide(const muniSegmenter* segmenter, uint8_t value, uint32_t codePoint)
{
    return IsBoundary(&segmenter->rules.grapheme, value, codePoint) ? muni_decideBreak
                                                                    : muni_decideJoin;
}

static uint8_t Lookup(uint32_t codePoint)
{
    return muniLookupGraphemeClusterBreak(codePoint);
}

muniResult muniNextGraphemeSegment(muniSegmenter* segmenter, size_t* offsetOut)
{
    // Decide never holds, so Decide stands in for the resolve rule too.
    return muniSegmenterNext(segmenter, offsetOut, Lookup, Decide, Decide, Absorb);
}
