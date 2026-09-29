// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rules X10, W1 to W7, N1, N2, I1 and I2 of UAX #9. Each isolating run
// sequence is found by walking the paragraph and resolved in a few
// passes over it; the levels stay those of the explicit phase until every
// sequence is resolved, since X10 computes sequences and sos and eos from
// them, and only then do I1 and I2 raise them.

#include "bidi_implicit.h"

#include "bidi_brackets.h"

static bool IsNeutral(uint8_t type)
{
    return type == muni_bcB || type == muni_bcS || type == muni_bcWs || type == muni_bcOn ||
           muniBidiIsIsolateInitiator(type) || type == muni_bcPdi;
}

// L for L; R for R, EN and AN (as N1 counts them); ON for the rest.
static uint8_t Strong(uint8_t type)
{
    if (type == muni_bcL)
    {
        return muni_bcL;
    }
    return type == muni_bcR || type == muni_bcEn || type == muni_bcAn ? muni_bcR : muni_bcOn;
}

// X10: the direction of the higher of two levels.
static uint8_t Boundary(uint8_t level, uint8_t other)
{
    return muniBidiLevelDirection(level > other ? level : other);
}

// X10: eos of the sequence whose last code point is at last.
static uint8_t EndOfSequence(const muniBidiParagraph* paragraph, size_t last)
{
    uint8_t level = paragraph->levels[last];
    size_t next = muniBidiNext(paragraph, last);
    if (next >= paragraph->length ||
        muniBidiIsIsolateInitiator(muniBidiOriginalType(paragraph, last)))
    {
        return Boundary(level, paragraph->level);
    }
    return Boundary(level, paragraph->levels[next]);
}

// W1 to W3 in one pass: W1 needs the previous type after W1, and W2 the
// last strong type before W3 turns AL into R.
static void ResolveMarksAndArabic(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    uint8_t previous = sequence->sos;
    uint8_t lastStrong = sequence->sos;
    for (size_t index = sequence->first; index < paragraph->length;
         index = muniBidiSequenceNext(paragraph, index))
    {
        uint8_t type = muniBidiType(paragraph, index);
        if (type == muni_bcNsm)
        {
            bool isolate = muniBidiIsIsolateInitiator(previous) || previous == muni_bcPdi;
            type = isolate ? muni_bcOn : previous; // W1
        }
        previous = type;
        if (type == muni_bcEn && lastStrong == muni_bcAl)
        {
            type = muni_bcAn; // W2
        }
        if (type == muni_bcL || type == muni_bcR || type == muni_bcAl)
        {
            lastStrong = type;
        }
        muniBidiSetType(paragraph, index, type == muni_bcAl ? muni_bcR : type); // W3
    }
}

// W4: a single separator between two numbers.
static void ResolveSeparators(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    uint8_t previous = muni_bcOn;
    for (size_t index = sequence->first; index < paragraph->length;
         index = muniBidiSequenceNext(paragraph, index))
    {
        uint8_t type = muniBidiType(paragraph, index);
        size_t next = muniBidiSequenceNext(paragraph, index);
        if ((type == muni_bcEs || type == muni_bcCs) && next < paragraph->length)
        {
            uint8_t following = muniBidiType(paragraph, next);
            if (type == muni_bcEs && previous == muni_bcEn && following == muni_bcEn)
            {
                type = muni_bcEn;
            }
            else if (type == muni_bcCs && previous == following &&
                     (previous == muni_bcEn || previous == muni_bcAn))
            {
                type = previous;
            }
            muniBidiSetType(paragraph, index, type);
        }
        previous = type;
    }
}

// W5 to W7 in one pass. A run of ET becomes EN when an EN touches it
// (W5) and ON otherwise (W6); W7 then turns EN after an L into L.
static void ResolveNumbers(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    uint8_t lastStrong = sequence->sos;
    uint8_t previous = muni_bcOn; // the previous type before W7
    size_t index = sequence->first;
    while (index < paragraph->length)
    {
        uint8_t type = muniBidiType(paragraph, index);
        size_t end = muniBidiSequenceNext(paragraph, index);
        if (type == muni_bcEt)
        {
            while (end < paragraph->length && muniBidiType(paragraph, end) == muni_bcEt)
            {
                end = muniBidiSequenceNext(paragraph, end);
            }
            bool number = previous == muni_bcEn ||
                          (end < paragraph->length && muniBidiType(paragraph, end) == muni_bcEn);
            type = number ? muni_bcEn : muni_bcOn;
        }
        else if (type == muni_bcEs || type == muni_bcCs)
        {
            type = muni_bcOn; // W6
        }
        previous = type;
        uint8_t resolved = type == muni_bcEn && lastStrong == muni_bcL ? muni_bcL : type;
        if (type == muni_bcL || type == muni_bcR)
        {
            lastStrong = type;
        }
        for (size_t k = index; k < end; k = muniBidiSequenceNext(paragraph, k))
        {
            muniBidiSetType(paragraph, k, resolved);
        }
        index = end;
    }
}

// N1 and N2: each run of neutrals takes the direction on both its sides
// when they agree, and the embedding direction otherwise.
static void ResolveNeutrals(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    uint8_t embedding = muniBidiLevelDirection(sequence->level);
    uint8_t before = sequence->sos;
    size_t index = sequence->first;
    while (index < paragraph->length)
    {
        uint8_t type = muniBidiType(paragraph, index);
        if (!IsNeutral(type))
        {
            before = Strong(type);
            index = muniBidiSequenceNext(paragraph, index);
            continue;
        }
        size_t last = index;
        size_t end = muniBidiSequenceNext(paragraph, index);
        while (end < paragraph->length && IsNeutral(muniBidiType(paragraph, end)))
        {
            last = end;
            end = muniBidiSequenceNext(paragraph, end);
        }
        uint8_t after = end < paragraph->length ? Strong(muniBidiType(paragraph, end))
                                                : EndOfSequence(paragraph, last);
        uint8_t direction = before == after ? after : embedding;
        for (size_t k = index; k < end; k = muniBidiSequenceNext(paragraph, k))
        {
            muniBidiSetType(paragraph, k, direction);
        }
        index = end;
    }
}

// Whether the level run starting at index starts an isolating run
// sequence: it does unless it starts with a PDI that matches an isolate
// initiator, found as the previous code point at or below its level.
static bool StartsSequence(const muniBidiParagraph* paragraph, size_t index)
{
    if (muniBidiOriginalType(paragraph, index) != muni_bcPdi)
    {
        return true;
    }
    uint8_t level = paragraph->levels[index];
    size_t previous = muniBidiPrevious(paragraph, index);
    while (previous != SIZE_MAX && paragraph->levels[previous] > level)
    {
        previous = muniBidiPrevious(paragraph, previous);
    }
    return previous == SIZE_MAX || paragraph->levels[previous] != level ||
           !muniBidiIsIsolateInitiator(muniBidiOriginalType(paragraph, previous));
}

static void ResolveSequence(const muniBidiParagraph* paragraph, size_t first, size_t previous)
{
    uint8_t level = paragraph->levels[first];
    uint8_t outside = previous == SIZE_MAX ? paragraph->level : paragraph->levels[previous];
    muniBidiSequence sequence = {paragraph, first, level, Boundary(level, outside)};
    ResolveMarksAndArabic(&sequence);
    ResolveSeparators(&sequence);
    ResolveNumbers(&sequence);
    muniBidiResolveBrackets(&sequence);
    ResolveNeutrals(&sequence);
}

// I1 and I2, then the levels of removed code points and continuation bytes.
static void ResolveLevels(const muniBidiParagraph* paragraph)
{
    uint8_t current = paragraph->level;
    for (size_t index = 0; index < paragraph->length; index++)
    {
        uint8_t type = muniBidiType(paragraph, index);
        if (type >= muni_bidiRemoved)
        {
            paragraph->levels[index] = current;
            continue;
        }
        uint8_t level = paragraph->levels[index];
        if ((level & 1) == 0)
        {
            level += type == muni_bcR ? 1 : type == muni_bcAn || type == muni_bcEn ? 2 : 0;
        }
        else if (type == muni_bcL || type == muni_bcEn || type == muni_bcAn)
        {
            level += 1;
        }
        paragraph->levels[index] = level;
        current = level;
    }
}

void muniBidiResolveImplicit(const muniBidiParagraph* paragraph)
{
    size_t previous = SIZE_MAX;
    for (size_t index = muniBidiFirst(paragraph); index < paragraph->length;
         index = muniBidiNext(paragraph, index))
    {
        bool runStart =
            previous == SIZE_MAX || paragraph->levels[previous] != paragraph->levels[index];
        if (runStart && StartsSequence(paragraph, index))
        {
            ResolveSequence(paragraph, index, previous);
        }
        previous = index;
    }
    ResolveLevels(paragraph);
}
