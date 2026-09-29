// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the parts of the bidi algorithm (UAX #9) share: the paragraph in
// the caller's memory and walks over its code points.
//
// The caller gives one level byte and one workspace byte per byte of
// text. Each code point keeps its data at its first byte. The workspace
// byte holds the code point's current bidi class in its low five bits,
// or one of two markers: muni_bidiContinuation on the other bytes of a
// code point, and muni_bidiRemoved on code points rule X9 removes. Its
// high bits flag matched brackets (BD16) and code points whose class was
// NSM before rule W1 (for the last part of rule N0).

#ifndef MAUL_UNICODE_SRC_BIDI_CORE_H
#define MAUL_UNICODE_SRC_BIDI_CORE_H

#include "encoding.h"
#include "tables.h"

#include "maul-unicode/properties.h"

enum
{
    muni_bidiClassBits = 0x1F,
    muni_bidiRemoved = 30,      // removed by rule X9
    muni_bidiContinuation = 31, // not the first byte of a code point
    muni_bidiOpening = 0x20,    // an opening bracket of a BD16 pair
    muni_bidiClosing = 0x40,    // a closing bracket of a BD16 pair
    muni_bidiWasNsm = 0x80,     // class NSM before rule W1
};

// The deepest valid embedding level (BD2).
#define MUNI_BIDI_MAX_DEPTH 125

// One paragraph in the caller's memory.
typedef struct muniBidiParagraph
{
    const uint8_t* text;
    size_t length; // the paragraph's length in bytes
    uint8_t* levels;
    uint8_t* workspace;
    uint8_t level; // the paragraph embedding level
} muniBidiParagraph;

static inline uint8_t muniBidiType(const muniBidiParagraph* paragraph, size_t index)
{
    return paragraph->workspace[index] & muni_bidiClassBits;
}

static inline void muniBidiSetType(const muniBidiParagraph* paragraph, size_t index, uint8_t type)
{
    uint8_t* slot = &paragraph->workspace[index];
    *slot = (uint8_t)((*slot & ~muni_bidiClassBits) | type);
}

// The code point starting at index, as the first pass decoded it: each
// maximal ill-formed subpart is U+FFFD.
static inline uint32_t muniBidiCodePoint(const uint8_t* text, size_t length, size_t index,
                                         size_t* sizeOut)
{
    uint32_t codePoint;
    (void)muniStepUtf8(text + index, length - index, &codePoint, sizeOut);
    return codePoint;
}

// The Bidi_Class the code point at index had before any rule changed it.
static inline uint8_t muniBidiOriginalType(const muniBidiParagraph* paragraph, size_t index)
{
    size_t size;
    return muniLookupBidiClass(muniBidiCodePoint(paragraph->text, paragraph->length, index, &size));
}

static inline bool muniBidiIsIsolateInitiator(uint8_t type)
{
    return type == muni_bcLri || type == muni_bcRli || type == muni_bcFsi;
}

// The next code point after index that rule X9 keeps, or the length.
static inline size_t muniBidiNext(const muniBidiParagraph* paragraph, size_t index)
{
    index += 1;
    while (index < paragraph->length && muniBidiType(paragraph, index) >= muni_bidiRemoved)
    {
        index += 1;
    }
    return index;
}

// The first code point that rule X9 keeps, or the length.
static inline size_t muniBidiFirst(const muniBidiParagraph* paragraph)
{
    if (paragraph->length == 0 || muniBidiType(paragraph, 0) < muni_bidiRemoved)
    {
        return 0;
    }
    return muniBidiNext(paragraph, 0);
}

// The previous code point before index that rule X9 keeps, or SIZE_MAX.
static inline size_t muniBidiPrevious(const muniBidiParagraph* paragraph, size_t index)
{
    while (index > 0)
    {
        index -= 1;
        if (muniBidiType(paragraph, index) < muni_bidiRemoved)
        {
            return index;
        }
    }
    return SIZE_MAX;
}

// L for an even level, R for an odd one (BD3).
static inline uint8_t muniBidiLevelDirection(uint8_t level)
{
    return (level & 1) != 0 ? muni_bcR : muni_bcL;
}

// An isolating run sequence (BD13): its first code point, its level and
// its sos, L or R.
typedef struct muniBidiSequence
{
    const muniBidiParagraph* paragraph;
    size_t first;
    uint8_t level;
    uint8_t sos;
} muniBidiSequence;

// The code point after index in its isolating run sequence, or the
// paragraph length after the last. Within a level run that is the next
// code point; at the end of one that closes on an isolate initiator, it
// is the matching PDI, the next code point at the initiator's level,
// since everything a valid isolate holds sits higher.
static inline size_t muniBidiSequenceNext(const muniBidiParagraph* paragraph, size_t index)
{
    size_t next = muniBidiNext(paragraph, index);
    uint8_t level = paragraph->levels[index];
    if (next >= paragraph->length || paragraph->levels[next] == level)
    {
        return next;
    }
    if (!muniBidiIsIsolateInitiator(muniBidiOriginalType(paragraph, index)))
    {
        return paragraph->length;
    }
    while (next < paragraph->length && paragraph->levels[next] > level)
    {
        next = muniBidiNext(paragraph, next);
    }
    bool matched = next < paragraph->length && paragraph->levels[next] == level &&
                   muniBidiOriginalType(paragraph, next) == muni_bcPdi;
    return matched ? next : paragraph->length;
}

#endif // MAUL_UNICODE_SRC_BIDI_CORE_H
