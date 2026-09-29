// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Normalization of text (UAX #15): each code point is decomposed fully
// and fed to the normalizer (src/normalizer.h), which orders and
// composes; and the quick check of section 9.

#include "maul-unicode/normalize.h"

#include "decompose.h"
#include "encoding.h"
#include "normalizer.h"
#include "tables.h"

static bool IsForm(muniNormalForm form)
{
    return form == muni_nfc || form == muni_nfd || form == muni_nfkc || form == muni_nfkd;
}

muniTextResult muniNormalize(const char* text, size_t length, muniNormalForm form,
                             muniConvertMode mode, char* output, size_t capacity, size_t* neededOut)
{
    if ((text == nullptr && length != 0) || (output == nullptr && capacity != 0) ||
        neededOut == nullptr || !IsForm(form) ||
        (mode != muni_convertStrict && mode != muni_convertReplace))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    muniNormalizer normalizer;
    muniNormalizerInit(&normalizer, form == muni_nfc || form == muni_nfkc, output, capacity);
    bool compatibility = form == muni_nfkc || form == muni_nfkd;
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
        uint32_t parts[MUNI_MAX_DECOMPOSITION];
        size_t count = 1;
        parts[0] = codePoint;
        if (codePoint >= 0xA0) // nothing below U+00A0 decomposes
        {
            count = muniDecomposeFully(codePoint, compatibility, parts);
        }
        for (size_t i = 0; i < count; i++)
        {
            if (!muniNormalizerAdd(&normalizer, parts[i]))
            {
                *neededOut = normalizer.writer.needed;
                return (muniTextResult){muni_errorLimit, offset};
            }
        }
        offset += size;
    }
    muniNormalizerFlush(&normalizer);
    *neededOut = normalizer.writer.needed;
    return (muniTextResult){normalizer.writer.needed > capacity ? muni_errorCapacity : muni_success,
                            length};
}

// The quick-check value of one code point (UAX #15 section 9 and the
// derivations tools/munigen/normalization.c checks against the UCD).
static muniQuickCheck QuickCheck(uint32_t codePoint, muniNormalForm form)
{
    uint32_t first;
    uint32_t second;
    bool canonical = muniCanonicalMapping(codePoint, &first, &second);
    switch (form)
    {
    case muni_nfd:
        return canonical ? muni_quickCheckNo : muni_quickCheckYes;
    case muni_nfkd:
        return canonical || muniUsesCompatibility(codePoint) ? muni_quickCheckNo
                                                             : muni_quickCheckYes;
    default:
        if ((canonical && muniIsCompositionExcluded(codePoint)) ||
            (form == muni_nfkc && muniUsesCompatibility(codePoint)))
        {
            return muni_quickCheckNo;
        }
        return muniCombinesBackward(codePoint) ? muni_quickCheckMaybe : muni_quickCheckYes;
    }
}

muniTextResult muniCheckNormalization(const char* text, size_t length, muniNormalForm form,
                                      muniQuickCheck* answerOut)
{
    if ((text == nullptr && length != 0) || answerOut == nullptr || !IsForm(form))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    const uint8_t* bytes = (const uint8_t*)text;
    muniQuickCheck answer = muni_quickCheckYes;
    uint8_t lastClass = 0;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(bytes + offset, length - offset, &codePoint, &size);
        if (status != muni_success)
        {
            return (muniTextResult){status, offset};
        }
        offset += size;
        if (codePoint < 0xA0)
        {
            lastClass = 0;
            continue;
        }
        uint8_t markClass = muniLookupCombiningClass(codePoint);
        muniQuickCheck check = QuickCheck(codePoint, form);
        if ((markClass != 0 && lastClass > markClass) || check == muni_quickCheckNo)
        {
            *answerOut = muni_quickCheckNo;
            return (muniTextResult){muni_success, length};
        }
        answer = check == muni_quickCheckMaybe ? check : answer;
        lastClass = markClass;
    }
    *answerOut = answer;
    return (muniTextResult){muni_success, length};
}

bool muniDecomposePair(uint32_t codePoint, uint32_t* firstOut, uint32_t* secondOut)
{
    uint32_t first;
    uint32_t second;
    if (firstOut == nullptr || secondOut == nullptr ||
        !muniCanonicalMapping(codePoint, &first, &second))
    {
        return false;
    }
    *firstOut = first;
    *secondOut = second;
    return true;
}

uint32_t muniComposePair(uint32_t first, uint32_t second)
{
    return muniComposeCanonical(first, second);
}
