// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A reading position in UTF-8 text that may arrive in pieces. It decodes
// one code point at a time, keeps the bytes of a sequence that a piece
// cuts off until the next piece completes it, and counts offsets from
// the start of the whole text.

#ifndef MAUL_UNICODE_SRC_CURSOR_H
#define MAUL_UNICODE_SRC_CURSOR_H

#include "encoding.h"

#include "maul-unicode/base.h"

typedef struct muniCursor
{
    const uint8_t* text;  // the current piece
    size_t length;        // its length in bytes
    size_t position;      // the next unread byte of the piece
    size_t offset;        // the offset in the whole text of the next code point
    uint8_t pending[4];   // the start of a sequence the previous piece cut off
    uint8_t pendingCount; // how many bytes of it there are
    bool moreFollows;     // whether more pieces will come
} muniCursor;

void muniCursorInit(muniCursor* cursor, const char* text, size_t length, bool moreFollows);

// Hands the cursor its next piece. Bytes still unread in the current
// piece are kept as pending. Returns false, changing nothing, when no
// piece was announced or when more than a cut-off sequence is unread,
// which happens only when the caller feeds before muni_needMoreText.
bool muniCursorFeed(muniCursor* cursor, const char* text, size_t length, bool moreFollows);

// The slow paths of muniCursorPeek and muniCursorAdvance: bytes pending
// from an earlier piece, or fewer than four left in this one.
muniResult muniCursorPeekSlow(const muniCursor* cursor, uint32_t* codePointOut, size_t* sizeOut);
void muniCursorAdvanceSlow(muniCursor* cursor, size_t size);

// Decodes the next code point without consuming it. Returns muni_success
// with the code point and its size in bytes, muni_done at the end of the
// whole text, or muni_needMoreText when the piece ends before the code
// point does and more pieces will come. An ill-formed sequence decodes as
// U+FFFD over its maximal subpart.
static inline muniResult muniCursorPeek(const muniCursor* cursor, uint32_t* codePointOut,
                                        size_t* sizeOut)
{
    size_t remaining = cursor->length - cursor->position;
    if (cursor->pendingCount == 0 && remaining >= 4)
    {
        (void)muniStepUtf8(cursor->text + cursor->position, remaining, codePointOut, sizeOut);
        return muni_success;
    }
    return muniCursorPeekSlow(cursor, codePointOut, sizeOut);
}

// Consumes the code point the last successful peek decoded.
static inline void muniCursorAdvance(muniCursor* cursor, size_t size)
{
    if (cursor->pendingCount == 0)
    {
        cursor->offset += size;
        cursor->position += size;
        return;
    }
    muniCursorAdvanceSlow(cursor, size);
}

#endif // MAUL_UNICODE_SRC_CURSOR_H
