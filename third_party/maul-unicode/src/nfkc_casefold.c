// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// NFKC_Casefold (UAX #44, DerivedNormalizationProps.txt): each code
// point maps to its NFKC_CF value, what compatibility decomposition,
// full case folding and removal of default ignorables leave when
// repeated until nothing changes, and the result is put in NFC. The
// mapping is found in decomposed form, which is canonically equivalent
// to the composed one of the UCD, so the normalizer's composition gives
// the same text; the test compares every code point with the UCD.

#include "case_map.h"
#include "decompose.h"
#include "encoding.h"
#include "normalizer.h"
#include "tables.h"

#include "maul-unicode/case.h"

// The most code points a mapping holds while it is found. The longest
// NFKC_CF value has 18 code points, U+FDFA's, and no step comes near 64.
#define MAX_MAPPING 64

// One round over a mapping: removes default ignorables, folds and
// decomposes the rest into to. Returns the new length, or MAX_MAPPING + 1
// when it would not fit; *changedOut tells whether anything changed.
static size_t Round(const uint32_t* from, size_t count, uint32_t* to, bool* changedOut)
{
    size_t written = 0;
    bool changed = false;
    for (size_t i = 0; i < count; i++)
    {
        if (muniLookupDefaultIgnorable(from[i]) != 0)
        {
            changed = true;
            continue;
        }
        uint32_t folded[MUNI_MAX_CASE_MAPPING];
        size_t foldedCount = muniMapCaseFully(from[i], muni_caseKindFold, folded);
        changed = changed || foldedCount != 1 || folded[0] != from[i];
        for (size_t k = 0; k < foldedCount; k++)
        {
            if (written + MUNI_MAX_DECOMPOSITION > MAX_MAPPING)
            {
                return MAX_MAPPING + 1;
            }
            size_t parts = muniDecomposeFully(folded[k], true, to + written);
            changed = changed || parts != 1 || to[written] != folded[k];
            written += parts;
        }
    }
    *changedOut = changed;
    return written;
}

// Writes the NFKC_CF mapping of codePoint, fully decomposed, into out,
// which holds MAX_MAPPING code points, and returns its length; returns
// MAX_MAPPING + 1 when it would not fit.
static size_t Map(uint32_t codePoint, uint32_t* out)
{
    uint32_t other[MAX_MAPPING];
    uint32_t* buffers[2] = {out, other};
    out[0] = codePoint;
    size_t count = 1;
    size_t current = 0;
    bool changed = true;
    while (changed)
    {
        count = Round(buffers[current], count, buffers[current ^ 1], &changed);
        current ^= 1;
        if (count > MAX_MAPPING)
        {
            return count;
        }
    }
    if (current != 0)
    {
        for (size_t i = 0; i < count; i++)
        {
            out[i] = other[i];
        }
    }
    return count;
}

// Feeds the mapping of one code point to the normalizer; false when it
// outgrows a limit.
static bool Feed(muniNormalizer* normalizer, uint32_t codePoint)
{
    if (codePoint < 0xA0)
    {
        // Below U+00A0 only A to Z change, and nothing is ignorable.
        uint32_t lower = codePoint - 'A' < 26 ? codePoint + 0x20 : codePoint;
        return muniNormalizerAdd(normalizer, lower);
    }
    uint32_t mapping[MAX_MAPPING];
    size_t count = Map(codePoint, mapping);
    if (count > MAX_MAPPING)
    {
        return false;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (!muniNormalizerAdd(normalizer, mapping[i]))
        {
            return false;
        }
    }
    return true;
}

muniTextResult muniToNfkcCasefold(const char* text, size_t length, muniConvertMode mode,
                                  char* output, size_t capacity, size_t* neededOut)
{
    if ((text == nullptr && length != 0) || (output == nullptr && capacity != 0) ||
        neededOut == nullptr || (mode != muni_convertStrict && mode != muni_convertReplace))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    muniNormalizer normalizer;
    muniNormalizerInit(&normalizer, true, output, capacity);
    const uint8_t* bytes = (const uint8_t*)text;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(bytes + offset, length - offset, &codePoint, &size);
        if (status != muni_success && mode == muni_convertStrict)
        {
            muniNormalizerFlush(&normalizer);
            *neededOut = normalizer.writer.needed;
            return (muniTextResult){status, offset};
        }
        if (!Feed(&normalizer, codePoint))
        {
            *neededOut = normalizer.writer.needed;
            return (muniTextResult){muni_errorLimit, offset};
        }
        offset += size;
    }
    muniNormalizerFlush(&normalizer);
    *neededOut = normalizer.writer.needed;
    return (muniTextResult){normalizer.writer.needed > capacity ? muni_errorCapacity : muni_success,
                            length};
}
