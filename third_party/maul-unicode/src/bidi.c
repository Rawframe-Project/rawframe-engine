// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// muniResolveBidi: one paragraph through the explicit and the implicit
// phases.

#include "maul-unicode/bidi.h"

#include "bidi_core.h"
#include "bidi_explicit.h"
#include "bidi_implicit.h"

#include <string.h>

muniResult muniResolveBidi(const char* text, size_t length, muniBidiDirection direction,
                           uint8_t* levelsOut, uint8_t* workspace, size_t* paragraphLengthOut,
                           uint8_t* paragraphLevelOut)
{
    bool missing = (text == nullptr || levelsOut == nullptr || workspace == nullptr) && length != 0;
    if (missing || paragraphLengthOut == nullptr || paragraphLevelOut == nullptr ||
        direction > muni_bidiRightToLeft)
    {
        return muni_errorInvalid;
    }
    if (length == 0)
    {
        *paragraphLengthOut = 0;
        *paragraphLevelOut = direction == muni_bidiRightToLeft ? 1 : 0;
        return muni_success;
    }
    const uint8_t* bytes = (const uint8_t*)text;
    muniBidiParagraph paragraph = {bytes, muniBidiParagraphLength(bytes, length), levelsOut,
                                   workspace, 0};
    if (direction == muni_bidiAuto)
    {
        paragraph.level = muniBidiFirstStrongLevel(&paragraph, 0, false, 0);
    }
    else
    {
        paragraph.level = direction == muni_bidiRightToLeft ? 1 : 0;
    }
    if (muniBidiResolveExplicit(&paragraph))
    {
        muniBidiResolveImplicit(&paragraph);
    }
    else
    {
        memset(levelsOut, 0, paragraph.length);
    }
    *paragraphLengthOut = paragraph.length;
    *paragraphLevelOut = paragraph.level;
    return muni_success;
}
