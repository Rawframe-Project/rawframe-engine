// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What deleting from an offset removes (record mui-0006): a grapheme
// cluster forward; back, a code point, but emoji, flags and keycaps whole,
// as Blink's and Android's Backspace delete.

#include "text_block.h"
#include "text_service.h"

#include "maul-ui/text_edit.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"
#include "maul-unicode/segment.h"

enum
{
    COMBINING_KEYCAP = 0x20E3,
};

// The grapheme cluster boundaries around offset: the last at or before
// it, and the first after it.
static void ClusterAround(const muiTextBlock* block, uint32_t offset, uint32_t* beforeOut,
                          uint32_t* afterOut)
{
    muniSegmentIterator iterator;
    (void)muniInitGraphemeIterator(&iterator, block->text.data, block->length, false);
    uint32_t before = 0;
    size_t at = 0;
    while (muniNextSegmentBreak(&iterator, &at) == muni_success && at <= offset)
    {
        before = (uint32_t)at;
    }
    *beforeOut = before;
    *afterOut = at > offset ? (uint32_t)at : block->length;
}

static bool IsVariationSelector(uint32_t point)
{
    return (point >= 0xFE00 && point <= 0xFE0F) || (point >= 0xE0100 && point <= 0xE01EF);
}

// Whether a code point makes its cluster go whole.
static bool IsWhole(uint32_t point)
{
    return muniIsExtendedPictographic(point) ||
           muniGetGraphemeBreak(point) == muni_gcbRegionalIndicator || point == COMBINING_KEYCAP ||
           point == '\r';
}

// Where Backspace from the end of a cluster stops: the cluster's start, or
// the last code point's, or the one before a variation selector.
static uint32_t BackspaceStart(const char* text, uint32_t start, uint32_t end)
{
    uint32_t last = start;
    uint32_t previous = start;
    uint32_t lastPoint = 0;
    for (uint32_t at = start; at < end;)
    {
        uint32_t point = 0;
        size_t size = 1;
        (void)muniDecodeUtf8(text + at, end - at, &point, &size);
        if (IsWhole(point))
        {
            return start;
        }
        previous = last;
        last = at;
        lastPoint = point;
        at += (uint32_t)size;
    }
    return IsVariationSelector(lastPoint) ? previous : last;
}

muiResult muiTextBlock_FindDeletion(const muiTextService* service, muiTextBlockId blockId,
                                    uint32_t offset, muiTextDeletion deletion, uint32_t* startOut,
                                    uint32_t* endOut)
{
    if (service == nullptr || blockId.index1 == 0 || deletion > mui_deleteForward ||
        startOut == nullptr || endOut == nullptr)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiPoolResolve(&service->blocks.pool, blockId.index1, blockId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiTextBlock* block = &service->blocks.blocks[slot - 1];
    offset = offset < block->length ? offset : block->length;
    uint32_t before = 0;
    uint32_t after = 0;
    ClusterAround(block, offset, &before, &after);
    if (deletion == mui_deleteForward)
    {
        *startOut = offset;
        *endOut = after;
        return mui_success;
    }
    // Back from inside a cluster deletes back to its start.
    uint32_t start = before;
    if (before == offset && offset != 0)
    {
        uint32_t previous = 0;
        uint32_t ignored = 0;
        ClusterAround(block, offset - 1, &previous, &ignored);
        start = BackspaceStart(block->text.data, previous, offset);
    }
    *startOut = start;
    *endOut = offset;
    return mui_success;
}
