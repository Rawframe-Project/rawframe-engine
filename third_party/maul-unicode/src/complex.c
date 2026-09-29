// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding SA runs in the current piece and asking the breaker about them.

#include "complex.h"

#include "encoding.h"
#include "tables.h"

#include "maul-unicode/properties.h"

// The breaker's next break after from, or the run's length when it gives
// none or gives one outside (from, length).
static size_t Ask(const muniComplexRun* run, size_t from)
{
    size_t next = run->breaker(run->context, (const char*)run->text, run->length, from);
    return next > from && next < run->length ? next : run->length;
}

// The length of the SA run at bytes, within the piece.
static size_t RunLength(const uint8_t* bytes, size_t available)
{
    size_t length = 0;
    while (length < available)
    {
        uint32_t codePoint;
        size_t size;
        if (muniStepUtf8(bytes + length, available - length, &codePoint, &size) ==
                muni_errorUtf8Truncated ||
            muniLookupLineBreak(codePoint) != muni_lbSa)
        {
            break;
        }
        length += size;
    }
    return length;
}

void muniComplexTrackRun(muniSegmenter* segmenter, uint32_t codePoint)
{
    muniComplexRun* run = &segmenter->complex;
    const muniCursor* cursor = &segmenter->cursor;
    if (run->text != nullptr && cursor->offset < run->start + run->length)
    {
        size_t relative = cursor->offset - run->start;
        if (run->next <= relative)
        {
            run->next = Ask(run, relative);
        }
        return;
    }
    run->text = nullptr;
    // A run starts at an SA code point read whole from this piece.
    if (cursor->pendingCount != 0 || muniLookupLineBreak(codePoint) != muni_lbSa)
    {
        return;
    }
    const uint8_t* bytes = cursor->text + cursor->position;
    size_t length = RunLength(bytes, cursor->length - cursor->position);
    if (length == 0)
    {
        return;
    }
    run->text = bytes;
    run->start = cursor->offset;
    run->length = length;
    run->next = Ask(run, 0);
}
