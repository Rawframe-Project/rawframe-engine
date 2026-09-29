// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Property lookups over the generated tables.

#include "maul-unicode/properties.h"

#include "tables.h"

muniGeneralCategory muniGetGeneralCategory(uint32_t codePoint)
{
    return muniLookupGeneralCategory(codePoint);
}

muniGraphemeBreak muniGetGraphemeBreak(uint32_t codePoint)
{
    return muniLookupGraphemeClusterBreak(codePoint);
}

muniWordBreak muniGetWordBreak(uint32_t codePoint)
{
    return muniLookupWordBreak(codePoint);
}

muniSentenceBreak muniGetSentenceBreak(uint32_t codePoint)
{
    return muniLookupSentenceBreak(codePoint);
}

muniIndicConjunctBreak muniGetIndicConjunctBreak(uint32_t codePoint)
{
    return muniLookupIndicConjunctBreak(codePoint);
}

bool muniIsExtendedPictographic(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiExtendedPictographic) != 0;
}

bool muniIsEmoji(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiEmoji) != 0;
}

bool muniIsEmojiPresentation(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiPresentation) != 0;
}

bool muniIsEmojiModifier(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiModifier) != 0;
}

bool muniIsEmojiModifierBase(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiModifierBase) != 0;
}

bool muniIsEmojiComponent(uint32_t codePoint)
{
    return (muniLookupEmoji(codePoint) & muni_emojiComponent) != 0;
}

bool muniIsWhiteSpace(uint32_t codePoint)
{
    return muniLookupWhiteSpace(codePoint) != 0;
}

bool muniIsDefaultIgnorable(uint32_t codePoint)
{
    return muniLookupDefaultIgnorable(codePoint) != 0;
}

int32_t muniGetDecimalDigitValue(uint32_t codePoint)
{
    if (codePoint - '0' < 10)
    {
        return (int32_t)(codePoint - '0');
    }
    if (codePoint < 0x80 || muniLookupGeneralCategory(codePoint) != muni_gcNd)
    {
        return -1;
    }
    // The greatest zero at or below the digit.
    uint32_t low = 0;
    uint32_t high = muniDecimalZeroCount;
    while (high - low > 1)
    {
        uint32_t middle = (low + high) / 2;
        if (muniDecimalZeros[middle] <= codePoint)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return (int32_t)(codePoint - muniDecimalZeros[low]);
}

muniLineBreak muniGetLineBreak(uint32_t codePoint)
{
    return muniLookupLineBreak(codePoint);
}

muniEastAsianWidth muniGetEastAsianWidth(uint32_t codePoint)
{
    return muniLookupEastAsianWidth(codePoint);
}

muniBidiClass muniGetBidiClass(uint32_t codePoint)
{
    return muniLookupBidiClass(codePoint);
}

uint32_t muniGetMirroringGlyph(uint32_t codePoint)
{
    uint8_t index = muniLookupBidiMirror(codePoint);
    return index == 0 ? codePoint
                      : (uint32_t)((int32_t)codePoint + muniBidiMirrorDeltas[index - 1]);
}

muniBracketType muniGetBracketType(uint32_t codePoint)
{
    return muniLookupBidiBracket(codePoint);
}

uint8_t muniGetCombiningClass(uint32_t codePoint)
{
    return muniLookupCombiningClass(codePoint);
}

muniScript muniGetScript(uint32_t codePoint)
{
    return muniScriptTags[muniLookupScript(codePoint)];
}

muniResult muniGetScriptExtensions(uint32_t codePoint, muniScript* scripts, size_t capacity,
                                   size_t* countOut)
{
    if ((scripts == nullptr && capacity != 0) || countOut == nullptr)
    {
        return muni_errorInvalid;
    }
    uint8_t set = muniLookupScriptExtensions(codePoint);
    if (set == 0)
    {
        if (capacity > 0)
        {
            scripts[0] = muniGetScript(codePoint);
        }
        *countOut = 1;
        return capacity > 0 ? muni_success : muni_errorCapacity;
    }
    size_t start = muniScriptSetStarts[set - 1];
    size_t count = muniScriptSetStarts[set] - start;
    for (size_t i = 0; i < count && i < capacity; i++)
    {
        scripts[i] = muniScriptTags[muniScriptSetMembers[start + i]];
    }
    *countOut = count;
    return count <= capacity ? muni_success : muni_errorCapacity;
}
