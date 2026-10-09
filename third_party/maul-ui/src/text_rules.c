// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A field's rules over text going in (record mui-0006). Control
// characters other than tab and line breaks go, as keys' text a platform
// delivers would otherwise type them; a single line takes each line
// break as a space, as Firefox's paste does; a number filter keeps the
// value a sign at the start then digits, with at most one point for a
// decimal, dropping the characters going in that would break it; a
// maximum length counts grapheme clusters, as Flutter's does, cutting
// what goes in to fit.

#include "text_rules.h"

#include "maul-ui/text_editor.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/segment.h"

#include <string.h>

enum
{
    NEXT_LINE = 0x85,
    LINE_SEPARATOR = 0x2028,
    PARAGRAPH_SEPARATOR = 0x2029,
};

static bool IsLineBreak(uint32_t point)
{
    return (point >= '\n' && point <= '\r') || point == NEXT_LINE || point == LINE_SEPARATOR ||
           point == PARAGRAPH_SEPARATOR;
}

static bool IsControl(uint32_t point)
{
    return (point < 0x20 && point != '\t' && !IsLineBreak(point)) || point == 0x7F;
}

// What a number's value holds around where text goes in: before it,
// whether anything and whether a point; after it, whether a sign starts
// it and whether it holds a point.
typedef struct Number
{
    bool any;
    bool point;
    bool signAfter;
    bool pointAfter;
} Number;

static Number NumberAround(const muiTextBlock* block, uint32_t start, uint32_t end)
{
    const char* text = block->text.data;
    uint32_t after = block->length - end;
    return (Number){
        .any = start != 0,
        .point = memchr(text, '.', start) != nullptr,
        .signAfter = after != 0 && (text[end] == '+' || text[end] == '-'),
        .pointAfter = memchr(text + end, '.', after) != nullptr,
    };
}

// Whether a character going in keeps the value a number, noting it.
static bool KeepsNumber(Number* number, muiTextFilter filter, uint32_t point)
{
    bool keeps = false;
    if (point == '+' || point == '-')
    {
        keeps = !number->any && !number->signAfter;
    }
    else if (point >= '0' && point <= '9')
    {
        keeps = !number->signAfter;
    }
    else if (point == '.')
    {
        keeps = filter == mui_filterDecimal && !number->point && !number->pointAfter &&
                !number->signAfter;
        number->point = number->point || keeps;
    }
    number->any = number->any || keeps;
    return keeps;
}

static uint32_t CountClusters(const char* text, uint32_t length)
{
    muniSegmentIterator iterator;
    if (length == 0 || muniInitGraphemeIterator(&iterator, text, length, false) != muni_success)
    {
        return 0;
    }
    uint32_t count = 0;
    size_t at = 0;
    // Every boundary ends a cluster: the text's start is never one.
    while (muniNextSegmentBreak(&iterator, &at) == muni_success)
    {
        count++;
    }
    return count;
}

// The length of the first clusters of a text, all of it when it has
// fewer.
static uint32_t ClustersLength(const char* text, uint32_t length, uint32_t clusters)
{
    muniSegmentIterator iterator;
    if (clusters == 0 || muniInitGraphemeIterator(&iterator, text, length, false) != muni_success)
    {
        return 0;
    }
    size_t at = 0;
    while (clusters != 0 && muniNextSegmentBreak(&iterator, &at) == muni_success)
    {
        clusters -= at != 0 ? 1u : 0u;
    }
    return (uint32_t)at;
}

// Writes the characters a field's rules let in, returning their length.
static uint32_t Filter(const muiTextBlock* block, uint32_t start, uint32_t end, const char* text,
                       size_t length, char* out)
{
    const muiTextEditDef* def = &block->editing.def;
    bool multiline = (def->flags & mui_editMultiline) != 0;
    Number number = NumberAround(block, start, end);
    uint32_t written = 0;
    for (size_t at = 0; at < length;)
    {
        uint32_t point = 0;
        size_t size = 1;
        (void)muniDecodeUtf8(text + at, length - at, &point, &size);
        bool keep = !IsControl(point);
        bool space = keep && !multiline && IsLineBreak(point);
        if (space)
        {
            // CR LF is one break, one space.
            size += point == '\r' && at + 1 < length && text[at + 1] == '\n' ? 1u : 0u;
            point = ' ';
        }
        if (keep && def->filter != mui_filterNone)
        {
            keep = KeepsNumber(&number, def->filter, point);
        }
        if (keep && space)
        {
            out[written++] = ' ';
        }
        else if (keep)
        {
            memcpy(out + written, text + at, size);
            written += (uint32_t)size;
        }
        at += size;
    }
    return written;
}

bool muiApplyEditRules(muiTextService* service, const muiTextBlock* block, uint32_t start,
                       uint32_t end, const char* text, size_t length, muiBuffer* out,
                       uint32_t* lengthOut)
{
    // Nothing grows: a break becomes one space.
    if (!muiReserve(&service->allocator, out, length + 1u))
    {
        return false;
    }
    char* kept = out->data;
    uint32_t written = Filter(block, start, end, text, length, kept);
    uint32_t limit = block->editing.def.maxLength;
    if (limit != 0 && written != 0)
    {
        const char* old = block->text.data;
        uint32_t held = CountClusters(old, start) + CountClusters(old + end, block->length - end);
        written = held >= limit ? 0 : ClustersLength(kept, written, limit - held);
    }
    *lengthOut = written;
    return true;
}
