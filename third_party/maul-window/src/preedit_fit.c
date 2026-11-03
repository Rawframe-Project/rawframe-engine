// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fitting a composition's offsets to its text, and dropping control
// characters.

#include "preedit_fit.h"

// Whether a byte continues a character of UTF-8.
static bool Continues(const char* text, size_t at)
{
    return ((unsigned char)text[at] & 0xC0u) == 0x80u;
}

// An offset within the text, moved back to the start of the character
// it falls in.
static size_t Floor(const char* text, size_t length, size_t offset)
{
    size_t at = offset < length ? offset : length;
    while (at > 0 && at < length && Continues(text, at))
    {
        at--;
    }
    return at;
}

// An offset within the text, moved on past the character it falls in.
static size_t Ceiling(const char* text, size_t length, size_t offset)
{
    size_t at = offset < length ? offset : length;
    while (at < length && Continues(text, at))
    {
        at++;
    }
    return at;
}

// The bytes of a control character at a byte of well-formed UTF-8: one
// for C0 and DEL, two for C1 (0xC2 then 0x80 to 0x9F), else 0.
static size_t ControlAt(const char* text, size_t length, size_t at)
{
    unsigned char byte = (unsigned char)text[at];
    if (byte < 0x20u || byte == 0x7Fu)
    {
        return 1;
    }
    return byte == 0xC2u && at + 1 < length && (unsigned char)text[at + 1] < 0xA0u ? 2 : 0;
}

// Where an offset of the text falls once its control characters go.
static size_t Kept(const char* text, size_t length, size_t offset)
{
    size_t gone = 0;
    for (size_t at = 0; at < length && at < offset;)
    {
        size_t control = ControlAt(text, length, at);
        if (control == 0)
        {
            at++;
            continue;
        }
        gone += offset - at < control ? offset - at : control;
        at += control;
    }
    return offset - gone;
}

size_t mwinKeptTextBytes(const char* text, size_t length)
{
    size_t kept = length;
    for (size_t at = 0; at < length;)
    {
        size_t control = ControlAt(text, length, at);
        kept -= control;
        at += control > 0 ? control : 1;
    }
    return kept;
}

void mwinCopyKeptText(char* out, const char* text, size_t length, mwinPreeditEvent* preedit,
                      mwinPreeditSegment* segments)
{
    size_t kept = 0;
    for (size_t at = 0; at < length;)
    {
        size_t control = ControlAt(text, length, at);
        if (control == 0)
        {
            out[kept++] = text[at];
        }
        at += control > 0 ? control : 1;
    }
    if (preedit == nullptr || kept == length)
    {
        return;
    }
    if (preedit->caret > 0)
    {
        preedit->caret = (ptrdiff_t)Kept(text, length, (size_t)preedit->caret);
    }
    preedit->selectionStart = Kept(text, length, preedit->selectionStart);
    preedit->selectionEnd = Kept(text, length, preedit->selectionEnd);
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        mwinPreeditSegment* segment = &segments[i];
        size_t past = segment->length < SIZE_MAX - segment->start ? segment->start + segment->length
                                                                  : SIZE_MAX;
        size_t from = Kept(text, length, segment->start);
        segment->length = Kept(text, length, past) - from;
        segment->start = from;
    }
}

void mwinFitPreedit(mwinPreeditEvent* preedit, mwinPreeditSegment* segments)
{
    const char* text = preedit->text;
    size_t length = preedit->length;
    if (preedit->caret >= 0)
    {
        preedit->caret = (ptrdiff_t)Floor(text, length, (size_t)preedit->caret);
    }
    else
    {
        preedit->caret = -1;
    }
    size_t start = preedit->selectionStart;
    size_t end = preedit->selectionEnd;
    preedit->selectionStart = Floor(text, length, start < end ? start : end);
    preedit->selectionEnd = Ceiling(text, length, start < end ? end : start);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        mwinPreeditSegment segment = segments[i];
        size_t from = Floor(text, length, segment.start);
        size_t past = segment.start < length && length - segment.start > segment.length
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
