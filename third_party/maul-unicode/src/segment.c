// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public segmentation API over the segmenter engine. The public
// iterator is opaque storage; the segmenter moves in and out of it with
// memcpy, which keeps the accesses within the C aliasing rules.

#include "maul-unicode/segment.h"

#include "segmenter.h"

#include <stddef.h>
#include <string.h>

static muniSegmenter Load(const muniSegmentIterator* iterator)
{
    muniSegmenter segmenter;
    memcpy(&segmenter, iterator, sizeof(segmenter));
    return segmenter;
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

muniResult muniSetComplexBreaker(muniSegmentIterator* iterator, muniComplexBreakFn breaker,
                                 void* context)
{
    if (iterator == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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

// The most overrides a table can hold: one per scalar value, since its
// code points strictly increase.
#define MAX_OVERRIDES 0x10F800u

// Whether a table of overrides is in strictly increasing order of code
// point and names only code points and known classes: up to classCount.
static bool ValidOverrides(const muniBreakClassOverride* overrides, size_t count,
                           uint8_t classCount)
{
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t codePoint = overrides[i].codePoint;
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            overrides[i].value >= classCount || (i > 0 && codePoint <= overrides[i - 1].codePoint))
        {
            return false;
        }
    }
    return true;
}

muniResult muniSetBreakClassOverrides(muniSegmentIterator* iterator,
                                      const muniBreakClassOverride* overrides, size_t count)
{
    if (iterator == nullptr || (overrides == nullptr && count != 0) || count > MAX_OVERRIDES)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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

muniResult muniSetLineOptions(muniSegmentIterator* iterator, uint32_t options)
{
    if (iterator == nullptr || (options & ~(uint32_t)muni_lineCjAsIdeographic) != 0)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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
    segmenter.options = (uint8_t)options;
    Store(iterator, &segmenter);
    return muni_success;
}

muniResult muniFeedSegmentIterator(muniSegmentIterator* iterator, const char* text, size_t length,
                                   bool moreFollows)
{
    if (iterator == nullptr || (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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
        return muniNextLineSegment(segmenter, offsetOut);
    default:
        return muni_errorInvalid;
    }
}

muniResult muniNextSegmentBreak(muniSegmentIterator* iterator, size_t* offsetOut)
{
    if (iterator == nullptr || offsetOut == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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

muniResult muniNextLineBreak(muniSegmentIterator* iterator, size_t* offsetOut, bool* mandatoryOut)
{
    if (iterator == nullptr || offsetOut == nullptr || mandatoryOut == nullptr)
    {
        return muni_errorInvalid;
    }
    muniSegmenter segmenter = Load(iterator);
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

muniResult muniFindGraphemeBreaks(const char* text, size_t length, size_t* offsets, size_t capacity,
                                  size_t* countOut)
{
    return Find(muni_segmentGrapheme, text, length, offsets, nullptr, capacity, countOut);
}

muniResult muniFindWordBreaks(const char* text, size_t length, size_t* offsets, size_t capacity,
                              size_t* countOut)
{
    return Find(muni_segmentWord, text, length, offsets, nullptr, capacity, countOut);
}

muniResult muniFindSentenceBreaks(const char* text, size_t length, size_t* offsets, size_t capacity,
                                  size_t* countOut)
{
    return Find(muni_segmentSentence, text, length, offsets, nullptr, capacity, countOut);
}

muniResult muniFindLineBreaks(const char* text, size_t length, size_t* offsets, bool* mandatory,
                              size_t capacity, size_t* countOut)
{
    return Find(muni_segmentLine, text, length, offsets, mandatory, capacity, countOut);
}
