// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's data by MIME type and the primary selection's text
// (mwin-0029), as the core keeps them for the backends.

#ifndef MAUL_WINDOW_SRC_CLIPBOARD_DATA_H
#define MAUL_WINDOW_SRC_CLIPBOARD_DATA_H

#include "core.h"

#include "maul-window/clipboard.h"

// An item of the data written: its MIME type with a NUL after it, and
// where its bytes lie after the items.
typedef struct mwinClipboardDataItem
{
    char mime[MWIN_CLIPBOARD_MIME + 1];
    uint32_t mimeLength;
    uint32_t offset;
    uint32_t length;
} mwinClipboardDataItem;

// The data written, text/plain left out (it is the clipboard's text),
// in one block from the allocator of size bytes, the items' bytes after
// it.
typedef struct mwinClipboardCopy
{
    size_t size;
    uint32_t count;
    mwinClipboardDataItem items[MWIN_CLIPBOARD_ITEMS];
} mwinClipboardCopy;

// The bytes of an item of the data written.
static inline const uint8_t* mwinClipboardBytesOf(const mwinClipboardCopy* copy,
                                                  const mwinClipboardDataItem* item)
{
    return (const uint8_t*)(copy + 1) + item->offset;
}

// The item of a MIME type the data written has, or NULL; and the type
// a data read asks for, the request's text.
const mwinClipboardDataItem* mwinFindClipboardItem(const mwinContext* context, const char* mime,
                                                   size_t length);

// Whether a type is text/plain, with parameters or none; whether two
// types are the same, ASCII case aside.
bool mwinIsPlainText(const char* mime, size_t length);
bool mwinSameMime(const char* a, size_t aLength, const char* b, size_t bLength);

// Whether the clipboard offers text: a write gave it text, or gave it no
// data. An empty text beside data is not offered.
bool mwinOffersClipboardText(const mwinContext* context);

// Whether a request is a window's last read of a kind (mwin_foundText
// and kin) answered done, and the payload that answered it is still the
// last of its kind.
bool mwinIsFoundBy(const mwinContext* context, mwinRequestId request, uint32_t kind);

// Copies out a read's payload for the getters: mwin_errorStale unless
// mwinIsFoundBy, as the text calls document.
mwinResult mwinCopyFound(const mwinContext* context, mwinRequestId request, uint32_t kind,
                         const void* found, size_t length, void* buffer, size_t capacity,
                         size_t* lengthOut);

// Frees the data written, as a text write replaces it.
void mwinReleaseClipboardData(mwinContext* context);

// Frees all of it; the context's end calls it.
void mwinReleaseClipboardExtras(mwinContext* context);

#endif // MAUL_WINDOW_SRC_CLIPBOARD_DATA_H
