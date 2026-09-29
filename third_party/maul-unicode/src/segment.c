// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public segmentation API over the segmenter engine. The public
// iterator is opaque storage; the segmenter moves in and out of it with
// memcpy, which keeps the accesses within the C aliasing rules.

#include "maul-unicode/segment.h"

#include "segmenter.h"

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
    if (!supported || segmenter.started || segmenter.waiting)
    {
        return muni_errorInvalid;
    }
    segmenter.complex.breaker = breaker;
    segmenter.complex.context = context;
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
    if (!segmenter.waiting || !muniCursorFeed(&segmenter.cursor, text, length, moreFollows))
    {
        return muni_errorInvalid;
    }
    segmenter.waiting = false;
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
    muniResult status = Next(&segmenter, offsetOut);
    segmenter.waiting = status == muni_needMoreText;
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
    muniResult status = Next(&segmenter, offsetOut);
    segmenter.waiting = status == muni_needMoreText;
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
