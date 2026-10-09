// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 clipboard: text as CF_UNICODETEXT and data by MIME type
// (mwin-0029), each type a registered format ("PNG" for image/png and
// "HTML Format" for text/html, as other programs name them), written and
// read at once while the request is submitted. Another program may hold
// the clipboard open for a moment, so opening it is tried a few times.

#ifndef MAUL_WINDOW_SRC_WIN32_CLIPBOARD_H
#define MAUL_WINDOW_SRC_WIN32_CLIPBOARD_H

#include "win32.h"

// Puts the context's written text and data on the clipboard.
mwinOutcome mwinWin32WriteClipboard(const mwinWin32Window* window);

// Takes the clipboard's data of the type a request names into the
// context; a clipboard without it fails.
mwinOutcome mwinWin32ReadClipboardData(const mwinWin32Window* window, const mwinRequest* request);

// Takes the clipboard's text into the context; no text reads as empty.
mwinOutcome mwinWin32ReadClipboard(const mwinWin32Window* window);

#endif // MAUL_WINDOW_SRC_WIN32_CLIPBOARD_H
