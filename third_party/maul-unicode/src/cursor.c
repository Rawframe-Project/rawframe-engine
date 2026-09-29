// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reading UTF-8 in pieces.

#include "cursor.h"

#include <string.h>

void muniCursorInit(muniCursor* cursor, const char* text, size_t length, bool moreFollows)
{
    memset(cursor, 0, sizeof(*cursor));
    cursor->text = (const uint8_t*)text;
    cursor->length = length;
    cursor->moreFollows = moreFollows;
}

bool muniCursorFeed(muniCursor* cursor, const char* text, size_t length, bool moreFollows)
{
    size_t unread = cursor->length - cursor->position;
    if (!cursor->moreFollows || cursor->pendingCount + unread >= sizeof(cursor->pending))
    {
        return false;
    }
    while (cursor->position < cursor->length && cursor->pendingCount < sizeof(cursor->pending))
    {
        cursor->pending[cursor->pendingCount] = cursor->text[cursor->position];
        cursor->pendingCount += 1;
        cursor->position += 1;
    }
    cursor->text = (const uint8_t*)text;
    cursor->length = length;
    cursor->position = 0;
    cursor->moreFollows = moreFollows;
    return true;
}

muniResult muniCursorPeekSlow(const muniCursor* cursor, uint32_t* codePointOut, size_t* sizeOut)
{
    // Up to four bytes: the pending ones, then the start of the piece.
    uint8_t window[4];
    size_t available = cursor->pendingCount;
    memcpy(window, cursor->pending, cursor->pendingCount);
    size_t fromPiece = cursor->length - cursor->position;
    size_t take = fromPiece < 4 - available ? fromPiece : 4 - available;
    if (cursor->text != nullptr && take > 0)
    {
        memcpy(window + available, cursor->text + cursor->position, take);
    }
    available += take;
    if (available == 0)
    {
        return cursor->moreFollows ? muni_needMoreText : muni_done;
    }
    muniResult status = muniStepUtf8(window, available, codePointOut, sizeOut);
    if (status == muni_errorUtf8Truncated && cursor->moreFollows && take == fromPiece)
    {
        return muni_needMoreText;
    }
    return muni_success;
}

void muniCursorAdvanceSlow(muniCursor* cursor, size_t size)
{
    cursor->offset += size;
    size_t fromPending = size < cursor->pendingCount ? size : cursor->pendingCount;
    memmove(cursor->pending, cursor->pending + fromPending, cursor->pendingCount - fromPending);
    cursor->pendingCount = (uint8_t)(cursor->pendingCount - fromPending);
    cursor->position += size - fromPending;
}
