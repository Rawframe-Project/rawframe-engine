// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs of Line_Break=SA text handed to a caller's breaker (muni-0005).
// Line and word rules call muniComplexTrack for each code point they
// read, and ask muniComplexBreakAt before deciding about one.

#ifndef MAUL_UNICODE_SRC_COMPLEX_H
#define MAUL_UNICODE_SRC_COMPLEX_H

#include "segmenter.h"

// What the breaker says about the position before the current code point.
typedef enum muniComplexAnswer
{
    muni_complexNone,  // not inside a run: the default rules decide
    muni_complexJoin,  // inside a run, no break
    muni_complexBreak, // inside a run, a break
} muniComplexAnswer;

// The work of muniComplexTrack when a breaker is set.
void muniComplexTrackRun(muniSegmenter* segmenter, uint32_t codePoint);

// Starts a run at an SA code point outside one, and inside one moves the
// next break past the current code point. Call before the cursor moves
// past the code point.
static inline void muniComplexTrack(muniSegmenter* segmenter, uint32_t codePoint)
{
    if (segmenter->complex.breaker != nullptr)
    {
        muniComplexTrackRun(segmenter, codePoint);
    }
}

static inline muniComplexAnswer muniComplexBreakAt(const muniSegmenter* segmenter)
{
    const muniComplexRun* run = &segmenter->complex;
    size_t offset = segmenter->cursor.offset;
    if (run->text == nullptr || offset <= run->start || offset >= run->start + run->length)
    {
        return muni_complexNone;
    }
    return offset - run->start == run->next ? muni_complexBreak : muni_complexJoin;
}

#endif // MAUL_UNICODE_SRC_COMPLEX_H
