// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fitting a composition's offsets to its text.

#include "preedit_fit.h"

// Whether a byte continues a character of UTF-8.
static bool Continues(const char* text, uint32_t at)
{
    return ((unsigned char)text[at] & 0xC0u) == 0x80u;
}

// An offset within the text, moved back to the start of the character
// it falls in.
static uint32_t Floor(const char* text, uint32_t length, uint32_t offset)
{
    uint32_t at = offset < length ? offset : length;
    while (at > 0 && at < length && Continues(text, at))
    {
        at--;
    }
    return at;
}

// An offset within the text, moved on past the character it falls in.
static uint32_t Ceiling(const char* text, uint32_t length, uint32_t offset)
{
    uint32_t at = offset < length ? offset : length;
    while (at < length && Continues(text, at))
    {
        at++;
    }
    return at;
}

void mwinFitPreedit(mwinPreeditEvent* preedit, mwinPreeditSegment* segments)
{
    const char* text = preedit->text;
    uint32_t length = preedit->length;
    if (preedit->caret >= 0)
    {
        preedit->caret = (int32_t)Floor(text, length, (uint32_t)preedit->caret);
    }
    else
    {
        preedit->caret = -1;
    }
    uint32_t start = preedit->selectionStart;
    uint32_t end = preedit->selectionEnd;
    preedit->selectionStart = Floor(text, length, start < end ? start : end);
    preedit->selectionEnd = Ceiling(text, length, start < end ? end : start);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        mwinPreeditSegment segment = segments[i];
        uint32_t from = Floor(text, length, segment.start);
        uint32_t past = segment.start < length && length - segment.start > segment.length
                            ? segment.start + segment.length
                            : length;
        past = Ceiling(text, length, past);
        if (past > from)
        {
            segments[kept++] = (mwinPreeditSegment){from, past - from, segment.style};
        }
    }
    preedit->segmentCount = kept;
    preedit->segments = segments;
}
