// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What deleting from an offset removes: a grapheme cluster forward; back,
// a code point, but emoji, flags and keycaps whole, as Blink's and
// Android's Backspace delete.

#include "text_delete.h"

#include "fpenv.h"
#include "text_block.h"
#include "text_blocks.h"
#include "text_service.h"

#include "maul-ui/text_edit.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"
#include "maul-unicode/segment.h"

#include <string.h>

enum
{
    COMBINING_KEYCAP = 0x20E3,
};

// The grapheme cluster boundaries around offset in a text: the last at or
// before it, and the first after it.
static void ClusterAround(const char* text, uint32_t length, uint32_t offset, uint32_t* beforeOut,
                          uint32_t* afterOut)
{
    muniSegmentIterator iterator;
    (void)muniInitGraphemeIterator(&iterator, text, length, false);
    uint32_t before = 0;
    size_t at = 0;
    while (muniSegmentIterator_NextBreak(&iterator, &at) == muni_success && at <= offset)
    {
        before = (uint32_t)at;
    }
    *beforeOut = before;
    *afterOut = at > offset ? (uint32_t)at : length;
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

static MUI_NOINLINE muiResult TextBlockFindDeletionWorker(const muiTextService* service,
                                                          muiTextBlockId blockId, size_t offset,
                                                          muiTextDeletion deletion,
                                                          size_t* startOut, size_t* endOut)
{
    if (startOut != nullptr)
    {
        *startOut = 0;
    }
    if (endOut != nullptr)
    {
        *endOut = 0;
    }
    if (service == nullptr || blockId.index1 == 0 || (uint32_t)deletion > mui_deleteForward ||
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
    uint32_t start = 0;
    uint32_t end = 0;
    muiFindDeletionIn(block->text.data, block->length, muiTextOffsetOf(offset), deletion, &start,
                      &end);
    *startOut = start;
    *endOut = end;
    return mui_success;
}

void muiFindDeletionIn(const char* text, uint32_t length, uint32_t offset, muiTextDeletion deletion,
                       uint32_t* startOut, uint32_t* endOut)
{
    offset = offset < length ? offset : length;
    uint32_t before = 0;
    uint32_t after = 0;
    ClusterAround(text, length, offset, &before, &after);
    if (deletion == mui_deleteForward)
    {
        *startOut = offset;
        *endOut = after;
        return;
    }
    // Back from inside a cluster deletes back to its start.
    uint32_t start = before;
    if (before == offset && offset != 0)
    {
        uint32_t previous = 0;
        uint32_t ignored = 0;
        ClusterAround(text, length, offset - 1, &previous, &ignored);
        start = BackspaceStart(text, previous, offset);
    }
    *startOut = start;
    *endOut = offset;
}

muiResult muiTextBlock_FindDeletion(const muiTextService* service, muiTextBlockId blockId,
                                    size_t offset, muiTextDeletion deletion, size_t* startOut,
                                    size_t* endOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result =
        TextBlockFindDeletionWorker(service, blockId, offset, deletion, startOut, endOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

uint32_t muiAfterComposition(const muiTextBlock* block)
{
    const char* text = block->text.data;
    uint32_t start = block->compositionStart;
    uint32_t end = start + block->compositionLength;
    uint32_t before = start < 4 ? start : 4;
    uint32_t after = block->length - end < 4 ? block->length - end : 4;
    char near[8];
    memcpy(near, text + start - before, before);
    memcpy(near + before, text + end, after);
    uint32_t at = before;
    while (!muiIsCharacterStartIn(near, before + after, at))
    {
        at++;
    }
    return start + at - before;
}

bool muiFindEditDeletion(muiTextService* service, const muiTextBlock* block,
                         muiTextDeletion deletion, uint32_t* startOut, uint32_t* endOut)
{
    uint32_t shown = block->compositionLength;
    if (shown == 0)
    {
        muiFindDeletionIn(block->text.data, block->length,
                          (uint32_t)block->editing.selection.caret.offset, deletion, startOut,
                          endOut);
        return true;
    }
    uint32_t composition = block->compositionStart;
    uint32_t length = block->length - shown;
    // At least a byte, so a text that is all composition has a buffer too.
    if (!muiReserve(&service->allocator, &service->removedScratch, (size_t)length + 1u))
    {
        return false;
    }
    char* without = service->removedScratch.data;
    const char* text = block->text.data;
    memcpy(without, text, composition);
    memcpy(without + composition, text + composition + shown, length - composition);
    muiFindDeletionIn(without, length, muiAfterComposition(block), deletion, startOut, endOut);
    return true;
}
