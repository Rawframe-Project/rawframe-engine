// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// BD16 and rule N0 of UAX #9. BD16 marks the brackets of each pair in the
// workspace instead of listing the pairs: pairs never cross, so the
// partner of a marked opening bracket is the marked closing bracket that
// balances it, and the stack limit of 63 bounds how deep that search
// nests. N0 then walks the sequence once, resolving each pair when it
// reaches the opening bracket, so a pair sees the directions earlier
// pairs gave their brackets, as the rule requires.

#include "bidi_brackets.h"

#define STACK_SIZE 63

typedef struct Opening
{
    uint32_t closer; // the bracket that closes it, canonically folded
    size_t index;
} Opening;

// Folds the canonical equivalents U+2329 and U+232A onto U+3008 and U+3009.
static uint32_t Fold(uint32_t codePoint)
{
    return codePoint == 0x2329 ? 0x3008 : codePoint == 0x232A ? 0x3009 : codePoint;
}

static void ClearMarks(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    for (size_t index = sequence->first; index < paragraph->length;
         index = muniBidiSequenceNext(paragraph, index))
    {
        paragraph->workspace[index] &= (uint8_t)~(muni_bidiOpening | muni_bidiClosing);
    }
}

// Pops the stack down to the opening that closer closes, marking the pair.
static void Close(const muniBidiParagraph* paragraph, Opening* stack, size_t* depth,
                  uint32_t closer, size_t index)
{
    for (size_t k = *depth; k > 0; k--)
    {
        if (stack[k - 1].closer == closer)
        {
            paragraph->workspace[stack[k - 1].index] |= muni_bidiOpening;
            paragraph->workspace[index] |= muni_bidiClosing;
            *depth = k - 1;
            return;
        }
    }
}

// BD16: marks the brackets of every pair; none when the stack overflows.
static void FindPairs(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    Opening stack[STACK_SIZE];
    size_t depth = 0;
    for (size_t index = sequence->first; index < paragraph->length;
         index = muniBidiSequenceNext(paragraph, index))
    {
        if (muniBidiType(paragraph, index) != muni_bcOn)
        {
            continue;
        }
        size_t size;
        uint32_t codePoint = muniBidiCodePoint(paragraph->text, paragraph->length, index, &size);
        uint8_t bracket = muniLookupBidiBracket(codePoint);
        if (bracket == muni_bracketOpen)
        {
            if (depth == STACK_SIZE)
            {
                ClearMarks(sequence);
                return;
            }
            stack[depth++] = (Opening){Fold(muniGetMirroringGlyph(codePoint)), index};
        }
        else if (bracket == muni_bracketClose)
        {
            Close(paragraph, stack, &depth, Fold(codePoint), index);
        }
    }
}

// L for L, R for R, EN and AN (which N0 counts as R), ON for the rest.
static uint8_t Strong(uint8_t type)
{
    if (type == muni_bcL)
    {
        return muni_bcL;
    }
    return type == muni_bcR || type == muni_bcEn || type == muni_bcAn ? muni_bcR : muni_bcOn;
}

// The closing bracket paired with the opening one at index.
static size_t Partner(const muniBidiParagraph* paragraph, size_t index)
{
    size_t depth = 0;
    for (size_t k = muniBidiSequenceNext(paragraph, index); k < paragraph->length;
         k = muniBidiSequenceNext(paragraph, k))
    {
        uint8_t marks = paragraph->workspace[k];
        if ((marks & muni_bidiOpening) != 0)
        {
            depth += 1;
        }
        else if ((marks & muni_bidiClosing) != 0)
        {
            if (depth == 0)
            {
                return k;
            }
            depth -= 1;
        }
    }
    return paragraph->length; // unreachable: every marked opening has a partner
}

// N0 items b to d: the direction the pair from opening to closing takes,
// or ON to leave it to N1 and N2.
static uint8_t PairDirection(const muniBidiParagraph* paragraph, size_t opening, size_t closing,
                             uint8_t embedding, uint8_t context)
{
    bool opposite = false;
    for (size_t k = muniBidiSequenceNext(paragraph, opening); k < closing;
         k = muniBidiSequenceNext(paragraph, k))
    {
        uint8_t strong = Strong(muniBidiType(paragraph, k));
        if (strong == embedding)
        {
            return embedding; // N0 b
        }
        opposite = opposite || strong != muni_bcOn;
    }
    if (!opposite)
    {
        return muni_bcOn; // N0 d
    }
    return context != embedding ? context : embedding; // N0 c
}

// Sets a bracket's direction and that of the NSMs that follow it.
static void SetBracket(const muniBidiParagraph* paragraph, size_t index, uint8_t direction)
{
    muniBidiSetType(paragraph, index, direction);
    for (size_t k = muniBidiSequenceNext(paragraph, index);
         k < paragraph->length && (paragraph->workspace[k] & muni_bidiWasNsm) != 0;
         k = muniBidiSequenceNext(paragraph, k))
    {
        muniBidiSetType(paragraph, k, direction);
    }
}

void muniBidiResolveBrackets(const muniBidiSequence* sequence)
{
    const muniBidiParagraph* paragraph = sequence->paragraph;
    FindPairs(sequence);
    uint8_t embedding = muniBidiLevelDirection(sequence->level);
    uint8_t context = sequence->sos;
    for (size_t index = sequence->first; index < paragraph->length;
         index = muniBidiSequenceNext(paragraph, index))
    {
        if ((paragraph->workspace[index] & muni_bidiOpening) != 0)
        {
            size_t closing = Partner(paragraph, index);
            uint8_t direction = PairDirection(paragraph, index, closing, embedding, context);
            if (direction != muni_bcOn)
            {
                SetBracket(paragraph, index, direction);
                SetBracket(paragraph, closing, direction);
            }
        }
        uint8_t strong = Strong(muniBidiType(paragraph, index));
        context = strong != muni_bcOn ? strong : context;
    }
}
