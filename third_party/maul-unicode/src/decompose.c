// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Single code point decomposition and composition over the rank indexes
// of the generated data: a code point's entry is found by its block's
// rank plus a count of set bits, and a pair's composite, which is not
// stored, by selecting the set bit that belongs to the pair's position.

#include "decompose.h"

#include "bits.h"
#include "tables.h"

enum
{
    HangulSBase = 0xAC00,
    HangulLBase = 0x1100,
    HangulVBase = 0x1161,
    HangulTBase = 0x11A7,
    HangulLCount = 19,
    HangulVCount = 21,
    HangulTCount = 28,
    HangulNCount = HangulVCount * HangulTCount,
    HangulSCount = HangulLCount * HangulNCount,
};

#define PAIR_EXCLUDED  0x10000000u
#define PAIR_KEY       0x0FFFFFFFu
#define MARK_COMPOSES  0x80000000u
#define MARK_CODEPOINT 0x001FFFFFu

// The code point at position of the canonical pairs: the inverse of
// muniRank.
static uint32_t PairSource(uint32_t position)
{
    uint32_t low = 0;
    uint32_t high = muniDecompositionBlockCount;
    while (high - low > 1)
    {
        uint32_t middle = (low + high) / 2;
        if (muniDecompositionPairRanks[middle] <= position)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    uint64_t map = muniDecompositionPairBits[low];
    for (uint32_t skip = position - muniDecompositionPairRanks[low]; skip > 0; skip--)
    {
        map &= map - 1;
    }
    return (uint32_t)muniDecompositionStarts[low] << 6 | muniLowestBit(map);
}

static bool IsHangulSyllable(uint32_t codePoint)
{
    return codePoint - HangulSBase < HangulSCount;
}

bool muniCanonicalMapping(uint32_t codePoint, uint32_t* firstOut, uint32_t* secondOut)
{
    if (IsHangulSyllable(codePoint))
    {
        uint32_t index = codePoint - HangulSBase;
        uint32_t trailing = index % HangulTCount;
        if (trailing != 0)
        {
            *firstOut = codePoint - trailing;
            *secondOut = HangulTBase + trailing;
        }
        else
        {
            *firstOut = HangulLBase + index / HangulNCount;
            *secondOut = HangulVBase + index % HangulNCount / HangulTCount;
        }
        return true;
    }
    uint8_t block = muniLookupDecompositionBlock(codePoint);
    int32_t pair =
        muniRank(block, codePoint, muniDecompositionPairBits, muniDecompositionPairRanks);
    if (pair >= 0)
    {
        uint32_t entry = muniDecompositionPairs[pair];
        *firstOut = (entry & PAIR_KEY) >> 7;
        *secondOut = muniDecompositionMarks[entry & 0x7F] & MARK_CODEPOINT;
        return true;
    }
    int32_t single =
        muniRank(block, codePoint, muniDecompositionSingleBits, muniDecompositionSingleRanks);
    if (single >= 0)
    {
        bool planeTwo = (muniDecompositionPlaneTwo[single >> 3] >> (single & 7) & 1) != 0;
        *firstOut = muniDecompositionSingles[single] | (planeTwo ? 0x20000u : 0);
        *secondOut = 0;
        return true;
    }
    return false;
}

// The index of mark among the pairs' second code points, or -1.
static int32_t MarkIndex(uint32_t mark)
{
    int32_t low = 0;
    int32_t high = (int32_t)muniDecompositionMarkCount - 1;
    while (low <= high)
    {
        int32_t middle = (low + high) / 2;
        uint32_t value = muniDecompositionMarks[middle] & MARK_CODEPOINT;
        if (value == mark)
        {
            return middle;
        }
        if (value < mark)
        {
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }
    return -1;
}

static uint32_t ComposeHangul(uint32_t first, uint32_t second)
{
    if (first - HangulLBase < HangulLCount && second - HangulVBase < HangulVCount)
    {
        return HangulSBase +
               ((first - HangulLBase) * HangulVCount + second - HangulVBase) * HangulTCount;
    }
    bool lv = IsHangulSyllable(first) && (first - HangulSBase) % HangulTCount == 0;
    if (lv && second - HangulTBase - 1 < HangulTCount - 1)
    {
        return first + second - HangulTBase;
    }
    return 0;
}

uint32_t muniComposeCanonical(uint32_t first, uint32_t second)
{
    uint32_t hangul = ComposeHangul(first, second);
    if (hangul != 0)
    {
        return hangul;
    }
    int32_t mark = MarkIndex(second);
    if (mark < 0 || (muniDecompositionMarks[mark] & MARK_COMPOSES) == 0 || first > 0x1FFFFF)
    {
        return 0;
    }
    uint32_t key = first << 7 | (uint32_t)mark;
    int32_t low = 0;
    int32_t high = (int32_t)muniCompositionCount - 1;
    while (low <= high)
    {
        int32_t middle = (low + high) / 2;
        uint16_t position = muniCompositionOrder[middle];
        uint32_t value = muniDecompositionPairs[position] & PAIR_KEY;
        if (value == key)
        {
            return PairSource(position);
        }
        if (value < key)
        {
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }
    return 0;
}

bool muniIsCompositionExcluded(uint32_t codePoint)
{
    uint8_t block = muniLookupDecompositionBlock(codePoint);
    if (muniRank(block, codePoint, muniDecompositionSingleBits, muniDecompositionSingleRanks) >= 0)
    {
        return true;
    }
    int32_t pair =
        muniRank(block, codePoint, muniDecompositionPairBits, muniDecompositionPairRanks);
    return pair >= 0 && (muniDecompositionPairs[pair] & PAIR_EXCLUDED) != 0;
}

// Whether codePoint is a second code point that composes: a mark of a
// composing pair, or a Hangul vowel or trailing consonant.
static bool Combines(uint32_t codePoint)
{
    if (codePoint - HangulVBase < HangulVCount || codePoint - HangulTBase - 1 < HangulTCount - 1)
    {
        return true;
    }
    int32_t mark = MarkIndex(codePoint);
    return mark >= 0 && (muniDecompositionMarks[mark] & MARK_COMPOSES) != 0;
}

bool muniCombinesBackward(uint32_t codePoint)
{
    // A composing pair whose first code point combines backward combines
    // too, as the Tulu-Tigalari vowel signs do; follow the first code
    // points down.
    for (;;)
    {
        if (Combines(codePoint))
        {
            return true;
        }
        uint32_t first;
        uint32_t second;
        if (IsHangulSyllable(codePoint) || !muniCanonicalMapping(codePoint, &first, &second) ||
            second == 0 || muniIsCompositionExcluded(codePoint))
        {
            return false;
        }
        codePoint = first;
    }
}

// The one-level compatibility mapping of codePoint into out; its length,
// 0 when it has none.
static size_t CompatibilityMapping(uint32_t codePoint, uint32_t* out)
{
    uint8_t block = muniLookupCompatibilityBlock(codePoint);
    int32_t rank = muniRank(block, codePoint, muniCompatibilityBits, muniCompatibilityRanks);
    if (rank < 0)
    {
        return 0;
    }
    size_t offset = muniCompatibilityOffsets[block - 1];
    for (int32_t k = muniCompatibilityRanks[block - 1]; k <= rank; k++)
    {
        size_t length = muniCompatibilityPool[offset++];
        for (size_t i = 0; i < length; i++)
        {
            uint32_t unit = muniCompatibilityPool[offset++];
            if (unit - 0xD800 < 0x400)
            {
                unit =
                    0x10000 + ((unit - 0xD800) << 10) + (muniCompatibilityPool[offset++] - 0xDC00);
            }
            if (k == rank)
            {
                out[i] = unit;
            }
        }
        if (k == rank)
        {
            return length;
        }
    }
    return 0; // unreachable: the rank lies in the block
}

// The work stack of the full decompositions: the most pending code
// points while decomposing is a mapping of 18 plus the few left above it.
#define WORK_DEPTH 40

bool muniUsesCompatibility(uint32_t codePoint)
{
    uint32_t work[WORK_DEPTH];
    size_t depth = 0;
    work[depth++] = codePoint;
    uint32_t scratch[MUNI_MAX_DECOMPOSITION];
    while (depth > 0)
    {
        uint32_t current = work[--depth];
        if (CompatibilityMapping(current, scratch) > 0)
        {
            return true;
        }
        uint32_t first;
        uint32_t second;
        if (muniCanonicalMapping(current, &first, &second) && depth + 2 <= WORK_DEPTH)
        {
            work[depth++] = first;
            if (second != 0)
            {
                work[depth++] = second;
            }
        }
    }
    return false;
}

size_t muniDecomposeFully(uint32_t codePoint, bool compatibility, uint32_t* out)
{
    uint32_t work[WORK_DEPTH];
    size_t depth = 0;
    size_t count = 0;
    work[depth++] = codePoint;
    while (depth > 0 && count < MUNI_MAX_DECOMPOSITION)
    {
        uint32_t current = work[--depth];
        uint32_t mapping[MUNI_MAX_DECOMPOSITION];
        size_t length = compatibility ? CompatibilityMapping(current, mapping) : 0;
        uint32_t first;
        uint32_t second;
        if (length == 0 && muniCanonicalMapping(current, &first, &second))
        {
            mapping[0] = first;
            mapping[1] = second;
            length = second != 0 ? 2 : 1;
        }
        if (length == 0 || depth + length > WORK_DEPTH)
        {
            out[count++] = current;
            continue;
        }
        for (size_t i = length; i > 0; i--)
        {
            work[depth++] = mapping[i - 1]; // the first on top
        }
    }
    return count;
}
