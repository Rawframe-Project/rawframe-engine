// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public segmentation API over the segmenter engine. The public
// iterator is opaque storage; the segmenter moves in and out of it with
// memcpy, which keeps the accesses within the C aliasing rules.

#include "maul-unicode/segment.h"

#include "encoding.h"
#include "segmenter.h"

#include <stddef.h>
#include <string.h>

// What an init call writes in the cursor's cookie, in bytes that were its
// padding, so that an iterator no init call set up is refused instead of
// read. Its top bit is set, so a small count or an ASCII byte pattern is
// no cookie; the checks after it keep any other bit pattern safe too.
#define COOKIE 0xC5E6u

// The most overrides a table can hold: one per scalar value, since its
// code points strictly increase.
#define MAX_OVERRIDES 0x10F800u

// The byte at offset in the iterator in caller memory. The flags are
// checked as these bytes, before the copy is read as bools. Reading them
// from the iterator rather than the copy just made of it keeps each load
// from waiting on the copy's stores.
static inline unsigned Byte(const muniSegmentIterator* iterator, size_t offset)
{
    return ((const unsigned char*)iterator)[offset];
}

// Whether the rules of a segmenter copied out of iterator hold what their
// rule set writes, each flag's byte a 0 or 1 included.
MUNI_NODISCARD static MUNI_ALWAYS_INLINE bool RulesValid(const muniSegmenter* segmenter,
                                                         const muniSegmentIterator* iterator)
{
    switch (segmenter->kind)
    {
    case muni_segmentGrapheme:
    {
        const muniGraphemeRules* rules = &segmenter->rules.grapheme;
        return segmenter->lineSettings == 0 && segmenter->holdKind == 0 &&
               rules->previous <= muni_gcbLvt && rules->emoji <= muni_graphemeEmojiLast &&
               (Byte(iterator, offsetof(muniSegmenter, rules.grapheme.linker)) |
                Byte(iterator, offsetof(muniSegmenter, rules.grapheme.oddRegional))) <= 1;
    }
    case muni_segmentWord:
    {
        const muniWordRules* rules = &segmenter->rules.word;
        uint32_t count = rules->overrideCount;
        return segmenter->lineSettings == 0 && segmenter->holdKind == 0 &&
               rules->actual <= muni_wbWSegSpace && rules->previous <= muni_wbWSegSpace &&
               rules->before <= muni_wbWSegSpace && rules->kind <= muni_wordIdeograph &&
               rules->reportedKind <= muni_wordIdeograph && count <= MAX_OVERRIDES &&
               (count == 0 || segmenter->overrides != nullptr) &&
               Byte(iterator, offsetof(muniSegmenter, rules.word.oddRegional)) <= 1;
    }
    case muni_segmentSentence:
    {
        const muniSentenceRules* rules = &segmenter->rules.sentence;
        uint32_t count = rules->overrideCount;
        return segmenter->lineSettings == 0 && segmenter->holdKind == 0 &&
               rules->actual <= muni_sbClose && rules->previous <= muni_sbClose &&
               rules->before <= muni_sbClose && rules->ending <= muni_sentenceEndingLast &&
               count <= MAX_OVERRIDES && (count == 0 || segmenter->overrides != nullptr) &&
               Byte(iterator, offsetof(muniSegmenter, rules.sentence.aterm)) <= 1;
    }
    case muni_segmentLine:
    {
        // Breaking anywhere keeps the grapheme rules where the other
        // settings keep the classes before the position.
        const muniLineRules* rules = &segmenter->rules.line;
        if (segmenter->lineSettings >> 3 != 0 || rules->actual > muni_lbHh)
        {
            return false;
        }
        if (muniLineStrictness(segmenter) == muni_lineBreakAnywhere)
        {
            return segmenter->holdKind == 0 && rules->grapheme.previous <= muni_gcbLvt &&
                   rules->grapheme.emoji <= muni_graphemeEmojiLast &&
                   (Byte(iterator, offsetof(muniSegmenter, rules.line.grapheme.linker)) |
                    Byte(iterator, offsetof(muniSegmenter, rules.line.grapheme.oddRegional))) <= 1;
        }
        return segmenter->holdKind <= muni_lineHoldLast && rules->previous <= muni_lbHh &&
               rules->before <= muni_lbHh && rules->lead <= muni_lbHh &&
               rules->number <= muni_lineNumberLast &&
               (Byte(iterator, offsetof(muniSegmenter, rules.line.leadInitialQuote)) |
                Byte(iterator, offsetof(muniSegmenter, rules.line.sawInfix)) |
                Byte(iterator, offsetof(muniSegmenter, rules.line.oddRegional))) <= 1;
    }
    default:
        return false;
    }
}

// Copies the iterator's state out of caller memory when it is one an
// init call set up and every flag, count and value in it is in range, so
// that no bit pattern makes a call read outside the state or the text
// and overrides it names; false otherwise. An iterator with the cookie
// whose fields were changed by hand is outside the contract, and how its
// fields agree with each other is not checked again. Every call reads
// most of the state, so all of it is checked, inline in each call so that
// the copy stays inline too.
MUNI_NODISCARD static MUNI_ALWAYS_INLINE bool Load(const muniSegmentIterator* iterator,
                                                   muniSegmenter* segmenterOut)
{
    memcpy(segmenterOut, iterator, sizeof(*segmenterOut));
    const muniSegmenter* segmenter = segmenterOut;
    const muniCursor* cursor = &segmenter->cursor;
    size_t at = offsetof(muniSegmenter, cursor);
    unsigned more = Byte(iterator, at + offsetof(muniCursor, moreFollows));
    unsigned started = Byte(iterator, offsetof(muniSegmenter, started));
    unsigned holding = Byte(iterator, offsetof(muniSegmenter, holding));
    if (cursor->cookie != COOKIE || segmenter->phase > muni_phaseBusy ||
        (more | started | holding | Byte(iterator, offsetof(muniSegmenter, endReported)) |
         Byte(iterator, offsetof(muniSegmenter, mandatory))) > 1 ||
        cursor->pendingCount > sizeof(cursor->pending) || cursor->position > cursor->length ||
        (cursor->text == nullptr && cursor->length != 0))
    {
        return false;
    }
    // A waiting cursor holds less than a code point, as muniCursorFeed
    // relies on; a boundary lies after a code point and a hold before the
    // cursor, as the engine's assertions do; an SA run starts at or before
    // the cursor, so the offset its breaker is asked about lies in the run.
    size_t offset = cursor->offset;
    const muniComplexRun* run = &segmenter->complex;
    if ((segmenter->phase == muni_phaseWaiting &&
         (more == 0 ||
          cursor->pendingCount + (cursor->length - cursor->position) >= sizeof(cursor->pending))) ||
        (started != 0 && offset == 0) ||
        (holding != 0 && (segmenter->heldOffset == 0 || segmenter->heldOffset >= offset)) ||
        (run->text != nullptr && (run->next > run->length || run->start > offset)))
    {
        return false;
    }
    return RulesValid(segmenter, iterator);
}

static void Store(muniSegmentIterator* iterator, const muniSegmenter* segmenter)
{
    memcpy(iterator, segmenter, sizeof(*segmenter));
}

// Marks the iterator in caller memory busy before a call that may run the
// caller's word segmenter, so a call the segmenter makes on it is refused
// instead of working on the state the outer call will overwrite. The
// outer call's Store clears the mark.
static void MarkBusy(muniSegmentIterator* iterator, const muniSegmenter* segmenter)
{
    if (segmenter->complex.breaker != nullptr)
    {
        uint8_t busy = muni_phaseBusy;
        memcpy((unsigned char*)iterator + offsetof(muniSegmenter, phase), &busy, sizeof(busy));
    }
}

static muniResult Init(muniSegmentIterator* iterator, uint8_t kind, const char* text, size_t length,
                       bool moreFollows)
{
    if (iterator == nullptr || (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    memset(&segmenter, 0, sizeof(segmenter));
    segmenter.kind = kind;
    muniCursorInit(&segmenter.cursor, text, length, moreFollows);
    segmenter.cursor.cookie = COOKIE;
    memset(iterator, 0, sizeof(*iterator));
    Store(iterator, &segmenter);
    return muni_success;
}

muniResult muniInitGraphemeIterator(muniSegmentIterator* iterator, const char* text, size_t length,
                                    bool moreFollows)
{
    return Init(iterator, muni_segmentGrapheme, text, length, moreFollows);
}

muniResult muniInitWordIterator(muniSegmentIterator* iterator, const char* text, size_t length,
                                bool moreFollows)
{
    return Init(iterator, muni_segmentWord, text, length, moreFollows);
}

muniResult muniInitSentenceIterator(muniSegmentIterator* iterator, const char* text, size_t length,
                                    bool moreFollows)
{
    return Init(iterator, muni_segmentSentence, text, length, moreFollows);
}

muniResult muniInitLineIterator(muniSegmentIterator* iterator, const char* text, size_t length,
                                bool moreFollows)
{
    return Init(iterator, muni_segmentLine, text, length, moreFollows);
}

muniResult muniSegmentIterator_SetComplexBreaker(muniSegmentIterator* iterator,
                                                 muniComplexBreakFunction breaker, void* context)
{
    if (iterator == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    bool supported = segmenter.kind == muni_segmentLine || segmenter.kind == muni_segmentWord;
    if (!supported)
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    if (segmenter.started || segmenter.phase == muni_phaseWaiting)
    {
        return muni_errorState;
    }
    segmenter.complex.breaker = breaker;
    segmenter.complex.context = context;
    Store(iterator, &segmenter);
    return muni_success;
}

// Whether a table of overrides is in strictly increasing order of code
// point and names only code points and known classes: up to classCount.
MUNI_NODISCARD static bool ValidOverrides(const muniBreakClassOverride* overrides, size_t count,
                                          uint8_t classCount)
{
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t codePoint = overrides[i].codePoint;
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            (uint32_t)overrides[i].value >= classCount ||
            (i > 0 && codePoint <= overrides[i - 1].codePoint))
        {
            return false;
        }
    }
    return true;
}

muniResult muniSegmentIterator_SetBreakClassOverrides(muniSegmentIterator* iterator,
                                                      const muniBreakClassOverride* overrides,
                                                      size_t count)
{
    if (iterator == nullptr || (overrides == nullptr && count != 0) || count > MAX_OVERRIDES)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    uint32_t* countField = nullptr;
    uint8_t classCount = 0;
    if (segmenter.kind == muni_segmentWord)
    {
        countField = &segmenter.rules.word.overrideCount;
        classCount = muni_wbWSegSpace + 1;
    }
    else if (segmenter.kind == muni_segmentSentence)
    {
        countField = &segmenter.rules.sentence.overrideCount;
        classCount = muni_sbClose + 1;
    }
    if (countField == nullptr || !ValidOverrides(overrides, count, classCount))
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    if (segmenter.started || segmenter.phase == muni_phaseWaiting)
    {
        return muni_errorState;
    }
    segmenter.overrides = count == 0 ? nullptr : overrides;
    *countField = (uint32_t)count;
    Store(iterator, &segmenter);
    return muni_success;
}

// Stores a line iterator's settings, after the checks every line
// setting makes: value from 0 to last, a line iterator, not busy, not
// started.
static muniResult SetLineSettings(muniSegmentIterator* iterator, int32_t value, int32_t last,
                                  uint8_t mask, uint8_t settings)
{
    if (iterator == nullptr || value < 0 || value > last)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    if (segmenter.kind != muni_segmentLine)
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    if (segmenter.started || segmenter.phase == muni_phaseWaiting)
    {
        return muni_errorState;
    }
    segmenter.lineSettings = (uint8_t)((segmenter.lineSettings & ~mask) | settings);
    Store(iterator, &segmenter);
    return muni_success;
}

muniResult muniSegmentIterator_SetLineBreakStrictness(muniSegmentIterator* iterator,
                                                      muniLineBreakStrictness strictness)
{
    return SetLineSettings(iterator, strictness, muni_lineBreakAnywhere, 3u,
                           (uint8_t)(strictness & 3));
}

muniLineBreakStrictness
muniSegmentIterator_GetLineBreakStrictness(const muniSegmentIterator* iterator)
{
    if (iterator == nullptr)
    {
        return muni_lineBreakNormal;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_lineBreakNormal;
    }
    return segmenter.kind == muni_segmentLine ? muniLineStrictness(&segmenter)
                                              : muni_lineBreakNormal;
}

muniResult muniSegmentIterator_SetWritingSystem(muniSegmentIterator* iterator,
                                                muniWritingSystem writingSystem)
{
    return SetLineSettings(iterator, writingSystem, muni_writingChineseJapanese, 4u,
                           (uint8_t)((writingSystem & 1) << 2));
}

muniWritingSystem muniSegmentIterator_GetWritingSystem(const muniSegmentIterator* iterator)
{
    if (iterator == nullptr)
    {
        return muni_writingOther;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_writingOther;
    }
    return segmenter.kind == muni_segmentLine ? muniLineWritingSystem(&segmenter)
                                              : muni_writingOther;
}

muniResult muniSegmentIterator_Feed(muniSegmentIterator* iterator, const char* text, size_t length,
                                    bool moreFollows)
{
    if (iterator == nullptr || (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    if (segmenter.phase != muni_phaseWaiting)
    {
        return muni_errorState;
    }
    muniCursorFeed(&segmenter.cursor, text, length, moreFollows);
    segmenter.phase = muni_phaseReady;
    Store(iterator, &segmenter);
    return muni_success;
}

static muniResult Next(muniSegmenter* segmenter, size_t* offsetOut)
{
    switch (segmenter->kind)
    {
    case muni_segmentGrapheme:
        return muniNextGraphemeSegment(segmenter, offsetOut);
    case muni_segmentWord:
        return muniNextWordSegment(segmenter, offsetOut);
    case muni_segmentSentence:
        return muniNextSentenceSegment(segmenter, offsetOut);
    case muni_segmentLine:
        // The default settings are 0: the default rules test no setting.
        if (segmenter->lineSettings == 0)
        {
            return muniNextLineSegment(segmenter, offsetOut);
        }
        return muniLineStrictness(segmenter) == muni_lineBreakAnywhere
                   ? muniNextLineAnywhereSegment(segmenter, offsetOut)
                   : muniNextLineTailoredSegment(segmenter, offsetOut);
    default:
        return muni_errorInvalid;
    }
}

muniResult muniSegmentIterator_NextBreak(muniSegmentIterator* iterator, size_t* offsetOut)
{
    if (iterator == nullptr || offsetOut == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    MarkBusy(iterator, &segmenter);
    muniResult status = Next(&segmenter, offsetOut);
    segmenter.phase = status == muni_needMoreText ? muni_phaseWaiting : muni_phaseReady;
    Store(iterator, &segmenter);
    return status;
}

muniResult muniSegmentIterator_NextLineBreak(muniSegmentIterator* iterator, size_t* offsetOut,
                                             bool* mandatoryOut)
{
    if (iterator == nullptr || offsetOut == nullptr || mandatoryOut == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    if (segmenter.kind != muni_segmentLine)
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    MarkBusy(iterator, &segmenter);
    muniResult status = Next(&segmenter, offsetOut);
    segmenter.phase = status == muni_needMoreText ? muni_phaseWaiting : muni_phaseReady;
    *mandatoryOut = status == muni_success && segmenter.mandatory;
    Store(iterator, &segmenter);
    return status;
}

static muniResult Find(uint8_t kind, const char* text, size_t length, size_t* offsets,
                       bool* mandatory, size_t capacity, size_t* countOut)
{
    if ((offsets == nullptr && capacity != 0) || countOut == nullptr ||
        (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    memset(&segmenter, 0, sizeof(segmenter));
    segmenter.kind = kind;
    muniCursorInit(&segmenter.cursor, text, length, false);
    size_t count = 0;
    size_t offset;
    while (Next(&segmenter, &offset) == muni_success)
    {
        if (count < capacity)
        {
            offsets[count] = offset;
            if (mandatory != nullptr)
            {
                mandatory[count] = segmenter.mandatory;
            }
        }
        count += 1;
    }
    *countOut = count;
    return count > capacity ? muni_errorCapacity : muni_success;
}

muniResult muniFindGraphemeBreaks(const char* text, size_t length, size_t* offsetsOut,
                                  size_t capacity, size_t* countOut)
{
    return Find(muni_segmentGrapheme, text, length, offsetsOut, nullptr, capacity, countOut);
}

muniResult muniFindWordBreaks(const char* text, size_t length, size_t* offsetsOut, size_t capacity,
                              size_t* countOut)
{
    return Find(muni_segmentWord, text, length, offsetsOut, nullptr, capacity, countOut);
}

muniResult muniFindSentenceBreaks(const char* text, size_t length, size_t* offsetsOut,
                                  size_t capacity, size_t* countOut)
{
    return Find(muni_segmentSentence, text, length, offsetsOut, nullptr, capacity, countOut);
}

muniResult muniFindLineBreaks(const char* text, size_t length, size_t* offsetsOut,
                              bool* mandatoryOut, size_t capacity, size_t* countOut)
{
    return Find(muni_segmentLine, text, length, offsetsOut, mandatoryOut, capacity, countOut);
}

muniResult muniSegmentIterator_GetWordKind(const muniSegmentIterator* iterator,
                                           muniWordKind* kindOut)
{
    if (iterator == nullptr || kindOut == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter;
    if (!Load(iterator, &segmenter))
    {
        return muni_errorInvalid;
    }
    if (segmenter.kind != muni_segmentWord)
    {
        return muni_errorInvalid;
    }
    if (segmenter.phase == muni_phaseBusy)
    {
        return muni_errorBusy;
    }
    *kindOut = segmenter.rules.word.reportedKind;
    return muni_success;
}

// The longest prefix of text that fits maxBytes and maxUnits and ends at
// a code point boundary, each code point weighing one unit, or with
// utf16, two past U+FFFF.
static size_t MeasureCodePoints(const char* text, size_t length, bool utf16, size_t maxBytes,
                                size_t maxUnits, size_t* unitsOut)
{
    size_t bytes = 0;
    size_t units = 0;
    while (bytes < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status =
            muniStepUtf8((const uint8_t*)text + bytes, length - bytes, &codePoint, &size);
        size_t weight = utf16 && status == muni_success && codePoint > 0xFFFF ? 2 : 1;
        if (size > maxBytes - bytes || weight > maxUnits - units)
        {
            break;
        }
        bytes += size;
        units += weight;
    }
    *unitsOut = units;
    return bytes;
}

// The same at grapheme cluster boundaries: the last that fits, found by
// reading up to the first one past it.
static size_t MeasureClusters(const char* text, size_t length, size_t maxBytes, size_t maxUnits,
                              size_t* unitsOut)
{
    muniSegmenter segmenter;
    memset(&segmenter, 0, sizeof(segmenter));
    segmenter.kind = muni_segmentGrapheme;
    muniCursorInit(&segmenter.cursor, text, length, false);
    size_t bytes = 0;
    size_t units = 0;
    size_t offset;
    while (units < maxUnits && Next(&segmenter, &offset) == muni_success && offset <= maxBytes)
    {
        bytes = offset;
        units++;
    }
    *unitsOut = units;
    return bytes;
}

muniResult muniMeasureUtf8(const char* text, size_t length, muniTextUnit unit, size_t maxBytes,
                           size_t maxUnits, size_t* bytesOut, size_t* unitsOut)
{
    if ((text == nullptr && length != 0) ||
        (unit != muni_unitCodePoint && unit != muni_unitGraphemeCluster && unit != muni_unitUtf16))
    {
        return muni_errorInvalid;
    }
    size_t bytes = length;
    size_t units = 0;
    if (maxBytes < length || maxUnits < length || unitsOut != nullptr)
    {
        // A cut that fits whole and counts nothing needs no walk: a unit is
        // at least a byte.
        bytes = unit == muni_unitGraphemeCluster
                    ? MeasureClusters(text, length, maxBytes, maxUnits, &units)
                    : MeasureCodePoints(text, length, unit == muni_unitUtf16, maxBytes, maxUnits,
                                        &units);
    }
    if (bytesOut != nullptr)
    {
        *bytesOut = bytes;
    }
    if (unitsOut != nullptr)
    {
        *unitsOut = units;
    }
    return muni_success;
}
