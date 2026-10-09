// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web clipboard: navigator.clipboard's writeText and readText, and
// for data by MIME type (mwin-0029) write and read with a ClipboardItem,
// a type other than text/plain, text/html and image/png a web custom
// format ("web " before it), answered when their promises settle. The
// browser may sanitize what it takes: image/png is decoded and encoded
// again. A refusal (NotAllowedError: the
// page has no focus, or the user said no) is mwin_outcomeDenied; a page
// without the Clipboard API, outside a secure context, has none.

#ifndef MAUL_WINDOW_SRC_WEB_CLIPBOARD_H
#define MAUL_WINDOW_SRC_WEB_CLIPBOARD_H

#include "web.h"

// Starts a write of the context's text, or a read: -1 while the page
// answers later, or the outcome now.
int mwinWebWriteClipboard(mwinContext* context, uint32_t slot);
int mwinWebReadClipboard(mwinContext* context, uint32_t slot);
int mwinWebWriteClipboardData(mwinContext* context, uint32_t slot);
int mwinWebReadClipboardData(mwinContext* context, uint32_t slot, const mwinRequest* request);

// Answers the request a clipboard record ends.
void mwinWebHandleClipboardRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

#endif // MAUL_WINDOW_SRC_WEB_CLIPBOARD_H
