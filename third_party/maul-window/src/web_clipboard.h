// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web clipboard: navigator.clipboard's writeText and readText,
// answered when their promises settle. A refusal (NotAllowedError: the
// page has no focus, or the user said no) is mwin_outcomeDenied; a page
// without the Clipboard API, outside a secure context, has none.

#ifndef MAUL_WINDOW_SRC_WEB_CLIPBOARD_H
#define MAUL_WINDOW_SRC_WEB_CLIPBOARD_H

#include "web.h"

// Starts a write of the context's text, or a read: -1 while the page
// answers later, or the outcome now.
int mwinWebWriteClipboard(mwinContext* context, uint32_t slot);
int mwinWebReadClipboard(mwinContext* context, uint32_t slot);

// Answers the request a clipboard record ends.
void mwinWebHandleClipboardRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

#endif // MAUL_WINDOW_SRC_WEB_CLIPBOARD_H
