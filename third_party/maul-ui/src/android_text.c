// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's text (record mui-0008, research
// 145): which text a node's granularities move through, the moves as
// AOSP's views make them (characters, lines and paragraphs on the
// boundaries every adapter shares, words on the tree's words, without
// the spaces after them), and the selection and text requests, in
// UTF-16 as Java counts. Text being edited moves its caret through the
// host; any other text a cursor the provider keeps.

#include "access_text.h"
#include "allocator.h"
#include "android.h"

#include <string.h>

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// Whether a node's name is its value text, byte for byte.
static bool NameIsValue(const muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    muiAccessText text = muiAccessValueOf(node);
    char small[256];
    size_t room = (size_t)text.length + 1;
    char* name = room <= sizeof small ? small : muiAllocate(&adapter->allocator, room, 1);
    size_t length = 0;
    bool same =
        name != nullptr &&
        muiAccessTree_GetName(adapter->tree, node->id, name, room, &length) == mui_success &&
        length == text.length && memcmp(name, text.bytes, length) == 0;
    if (name != nullptr && name != small)
    {
        muiRelease(&adapter->allocator, name, room, 1);
    }
    return same;
}

bool muiAndroidIsTraversable(const muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    const char* value = node->text[mui_accessValue];
    if (value == nullptr || node->textLength[mui_accessValue] == 0)
    {
        return false;
    }
    muiAndroidText shown = muiAndroidTextOf(node, MUI_ANDROID_TEXT);
    return shown.name ? NameIsValue(adapter, node) : shown.text == value;
}

// A password moves by character alone, as Android's own fields do.
jint muiAndroidGranularitiesOf(const muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    if (!muiAndroidIsTraversable(adapter, node))
    {
        return 0;
    }
    if (node->role == mui_rolePasswordInput)
    {
        return MUI_ANDROID_CHARACTER;
    }
    return MUI_ANDROID_CHARACTER | MUI_ANDROID_LINE | MUI_ANDROID_PARAGRAPH |
           (node->marks.wordCount != 0 ? MUI_ANDROID_WORD : 0);
}

static jint UnitsBefore(const muiAccessText* text, uint32_t byte)
{
    return (jint)muiAccessUtf16Before(text->bytes, byte);
}

// The unit following a byte offset or preceding it, as AOSP's iterators
// give one: the unit starting at it or ending at it, else the next or
// the one before, when it is inside one. False past the text's end that
// way.
static bool UnitSegment(const muiAccessText* text, muiAccessUnit unit, bool forward, uint32_t at,
                        uint32_t* startOut, uint32_t* endOut)
{
    bool boundary =
        at == 0 || at == text->length ||
        muiAccessBoundaryAfter(text, unit, muiAccessBoundaryBefore(text, unit, at)) == at;
    if (forward)
    {
        uint32_t start = boundary ? at : muiAccessBoundaryAfter(text, unit, at);
        if (start >= text->length)
        {
            return false;
        }
        *startOut = start;
        *endOut = muiAccessBoundaryAfter(text, unit, start);
        return true;
    }
    uint32_t end = boundary ? at : muiAccessBoundaryBefore(text, unit, at);
    if (end == 0)
    {
        return false;
    }
    *startOut = muiAccessBoundaryBefore(text, unit, end);
    *endOut = end;
    return true;
}

// The word following a byte offset, from the offset when it is inside
// one; or the word preceding it, to the offset.
static bool WordSegment(const muiAccessTextMarks* marks, bool forward, uint32_t at,
                        uint32_t* startOut, uint32_t* endOut)
{
    if (forward)
    {
        for (uint32_t i = 0; i < marks->wordCount; i++)
        {
            if (marks->words[i].end > at)
            {
                *startOut = marks->words[i].start > at ? marks->words[i].start : at;
                *endOut = marks->words[i].end;
                return true;
            }
        }
        return false;
    }
    for (uint32_t i = marks->wordCount; i-- > 0;)
    {
        if (marks->words[i].start < at)
        {
            *startOut = marks->words[i].start;
            *endOut = marks->words[i].end < at ? marks->words[i].end : at;
            return true;
        }
    }
    return false;
}

static bool Segment(const muiAccessText* text, jint granularity, bool forward, uint32_t at,
                    uint32_t* startOut, uint32_t* endOut)
{
    switch (granularity)
    {
    case MUI_ANDROID_CHARACTER:
        return UnitSegment(text, mui_unitCharacter, forward, at, startOut, endOut);
    case MUI_ANDROID_WORD:
        return WordSegment(text->marks, forward, at, startOut, endOut);
    case MUI_ANDROID_LINE:
        return UnitSegment(text, mui_unitLine, forward, at, startOut, endOut);
    case MUI_ANDROID_PARAGRAPH:
        return UnitSegment(text, mui_unitParagraph, forward, at, startOut, endOut);
    default:
        return false;
    }
}

static bool AskSelection(const muiAndroidAdapter* adapter, const muiAccessNode* node,
                         uint32_t anchor, uint32_t focus)
{
    const muiAccessRequest request = {
        .action = mui_actionSetSelection, .target = node->id, .anchor = anchor, .focus = focus};
    return adapter->action(adapter->user, &request);
}

bool muiAndroidTraverse(const muiAndroidAdapter* adapter, const muiAccessNode* node,
                        jint granularity, bool forward, bool extend, jint cursor, jint moved[4])
{
    if ((muiAndroidGranularitiesOf(adapter, node) & granularity) == 0)
    {
        return false;
    }
    muiAccessText text = muiAccessValueOf(node);
    const muiAccessTextMarks* marks = &node->marks;
    bool caret = marks->selected && Has(node, mui_actionSetSelection);
    uint32_t at = caret         ? marks->focus
                  : cursor >= 0 ? muiAccessByteOfUtf16(&text, (uint32_t)cursor)
                  : forward     ? 0
                                : text.length;
    uint32_t start = 0;
    uint32_t end = 0;
    if (!Segment(&text, granularity, forward, at, &start, &end))
    {
        return false;
    }
    uint32_t focus = forward ? end : start;
    uint32_t anchor = caret && extend ? marks->anchor : focus;
    if (caret && !AskSelection(adapter, node, anchor, focus))
    {
        return false;
    }
    moved[0] = UnitsBefore(&text, start);
    moved[1] = UnitsBefore(&text, end);
    moved[2] = UnitsBefore(&text, anchor);
    moved[3] = UnitsBefore(&text, focus);
    return true;
}

// Without a start, the selection collapses to the caret, as Flutter
// reads a request with no arguments; one past the text is refused.
bool muiAndroidSelect(const muiAndroidAdapter* adapter, const muiAccessNode* node, jint start,
                      jint end)
{
    if (!Has(node, mui_actionSetSelection))
    {
        return false;
    }
    if (start < 0)
    {
        return AskSelection(adapter, node, node->marks.focus, node->marks.focus);
    }
    muiAccessText text = muiAccessValueOf(node);
    jint units = UnitsBefore(&text, text.length);
    if (end < 0 || start > units || end > units)
    {
        return false;
    }
    return AskSelection(adapter, node, muiAccessByteOfUtf16(&text, (uint32_t)start),
                        muiAccessByteOfUtf16(&text, (uint32_t)end));
}

// UTF-16 as UTF-8, a lone surrogate as U+FFFD; the bytes written, at
// most 3 a unit.
static size_t Utf8Of(const jchar* units, jsize count, char* out)
{
    size_t written = 0;
    for (jsize i = 0; i < count; i++)
    {
        uint32_t point = units[i];
        bool high = point >= 0xD800 && point < 0xDC00;
        bool paired = high && i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000;
        if (paired)
        {
            point = 0x10000 + ((point - 0xD800) << 10) + (units[++i] - 0xDC00u);
        }
        else if (point >= 0xD800 && point < 0xE000)
        {
            point = 0xFFFD;
        }
        if (point < 0x80)
        {
            out[written++] = (char)point;
        }
        else if (point < 0x800)
        {
            out[written++] = (char)(0xC0 | (point >> 6));
            out[written++] = (char)(0x80 | (point & 0x3F));
        }
        else if (point < 0x10000)
        {
            out[written++] = (char)(0xE0 | (point >> 12));
            out[written++] = (char)(0x80 | ((point >> 6) & 0x3F));
            out[written++] = (char)(0x80 | (point & 0x3F));
        }
        else
        {
            out[written++] = (char)(0xF0 | (point >> 18));
            out[written++] = (char)(0x80 | ((point >> 12) & 0x3F));
            out[written++] = (char)(0x80 | ((point >> 6) & 0x3F));
            out[written++] = (char)(0x80 | (point & 0x3F));
        }
    }
    return written;
}

bool muiAndroidSetText(const muiAndroidAdapter* adapter, const muiAccessNode* node,
                       const jchar* units, jsize count)
{
    if (!Has(node, mui_actionReplaceText) || count < 0)
    {
        return false;
    }
    size_t room = (size_t)count * 3 + 1;
    char* bytes = muiAllocate(&adapter->allocator, room, 1);
    if (bytes == nullptr)
    {
        return false;
    }
    size_t length = Utf8Of(units, count, bytes);
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = node->id,
                                      .anchor = 0,
                                      .focus = muiAccessValueOf(node).length,
                                      .text = bytes,
                                      .length = (uint32_t)length};
    bool done = adapter->action(adapter->user, &request);
    muiRelease(&adapter->allocator, bytes, room, 1);
    return done;
}
