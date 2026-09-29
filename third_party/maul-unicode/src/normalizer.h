// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A streaming normalizer, in one pass. The caller decomposes each code
// point fully; the parts go into a segment: a starter (combining class 0)
// and the marks after it, which insertion keeps in canonical order. A new
// starter ends the segment; in the composing forms the segment is
// composed first, and when it composed into a lone starter, that starter
// may compose with the new one too, as Hangul LV + T and U+0B47 + U+0B3E
// do. Then the segment is written out and the new starter begins the
// next.

#ifndef MAUL_UNICODE_SRC_NORMALIZER_H
#define MAUL_UNICODE_SRC_NORMALIZER_H

#include "decompose.h"
#include "tables.h"
#include "writer.h"

#include <stddef.h>
#include <stdint.h>

// The most code points a segment holds: a starter and the marks after it.
#define MUNI_MAX_SEGMENT 32

typedef struct muniNormalizer
{
    uint32_t segment[MUNI_MAX_SEGMENT];
    uint8_t classes[MUNI_MAX_SEGMENT];
    size_t count;
    bool compose;
    muniWriter writer;
} muniNormalizer;

// Starts a normalizer writing into output; compose selects NFC over NFD.
static inline void muniNormalizerInit(muniNormalizer* normalizer, bool compose, char* output,
                                      size_t capacity)
{
    normalizer->count = 0;
    normalizer->compose = compose;
    normalizer->writer = (muniWriter){output, capacity, 0};
}

// Canonical composition of the segment (UAX #15 D117): each mark that no
// uncomposed mark before it blocks tries to compose with the starter.
static inline void muniComposeSegment(muniNormalizer* normalizer)
{
    if (normalizer->count < 2 || normalizer->classes[0] != 0)
    {
        return;
    }
    uint32_t starter = normalizer->segment[0];
    size_t kept = 1;
    uint8_t lastKept = 0; // the class of the last uncomposed mark, 0 for none
    for (size_t i = 1; i < normalizer->count; i++)
    {
        uint32_t mark = normalizer->segment[i];
        uint8_t markClass = normalizer->classes[i];
        bool blocked = lastKept != 0 && lastKept >= markClass;
        uint32_t composite = blocked ? 0 : muniComposeCanonical(starter, mark);
        if (composite != 0)
        {
            starter = composite;
            continue;
        }
        normalizer->segment[kept] = mark;
        normalizer->classes[kept] = markClass;
        kept += 1;
        lastKept = markClass;
    }
    normalizer->segment[0] = starter;
    normalizer->count = kept;
}

// Writes out the segment in hand, at the end of the text or before an
// error.
static inline void muniNormalizerFlush(muniNormalizer* normalizer)
{
    if (normalizer->compose)
    {
        muniComposeSegment(normalizer);
    }
    for (size_t i = 0; i < normalizer->count; i++)
    {
        muniWriterPut(&normalizer->writer, normalizer->segment[i]);
    }
    normalizer->count = 0;
}

// Adds one code point of a full decomposition; false when the segment
// would outgrow MUNI_MAX_SEGMENT.
static inline bool muniNormalizerAdd(muniNormalizer* normalizer, uint32_t codePoint)
{
    uint8_t markClass = codePoint < 0x300 ? 0 : muniLookupCombiningClass(codePoint);
    if (markClass == 0)
    {
        // Nothing below U+0300 composes with what precedes it.
        if (normalizer->compose && normalizer->count > 0 && codePoint >= 0x300)
        {
            muniComposeSegment(normalizer);
            uint32_t composite = normalizer->count == 1 && normalizer->classes[0] == 0
                                     ? muniComposeCanonical(normalizer->segment[0], codePoint)
                                     : 0;
            if (composite != 0)
            {
                normalizer->segment[0] = composite;
                return true;
            }
        }
        muniNormalizerFlush(normalizer);
        normalizer->segment[0] = codePoint;
        normalizer->classes[0] = 0;
        normalizer->count = 1;
        return true;
    }
    if (normalizer->count == MUNI_MAX_SEGMENT)
    {
        return false;
    }
    // Canonical ordering: after every mark of the same or a lower class.
    size_t at = normalizer->count;
    while (at > 0 && normalizer->classes[at - 1] > markClass)
    {
        normalizer->segment[at] = normalizer->segment[at - 1];
        normalizer->classes[at] = normalizer->classes[at - 1];
        at -= 1;
    }
    normalizer->segment[at] = codePoint;
    normalizer->classes[at] = markClass;
    normalizer->count += 1;
    return true;
}

#endif // MAUL_UNICODE_SRC_NORMALIZER_H
