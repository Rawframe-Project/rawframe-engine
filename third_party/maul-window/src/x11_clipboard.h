// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 clipboard: the CLIPBOARD selection, owned and received by a
// hidden window the context makes at the clipboard's first use. A write
// takes the selection, quoting the time of the latest key or button
// event, and answers other clients' requests for TARGETS, TIMESTAMP,
// UTF8_STRING and text/plain;charset=utf-8, in pieces (INCR) past 64
// KiB. A read converts the selection to UTF8_STRING and takes the text
// whole or in pieces, bounded by the limit, failing after a deadline;
// while the program owns the selection, a read answers from its own
// text at once.

#ifndef MAUL_WINDOW_SRC_X11_CLIPBOARD_H
#define MAUL_WINDOW_SRC_X11_CLIPBOARD_H

#include "x11.h"

// Destroys the hidden window and gives back a read's text.
void mwinX11StopClipboard(mwinX11Platform* platform);

// Carries out a clipboard request: its outcome, or -1 while the read
// goes on.
int mwinX11WriteClipboard(mwinX11Platform* platform);
int mwinX11ReadClipboard(mwinX11Platform* platform);

// Handles a selection event, or a property event of a transfer: true
// when it was one.
bool mwinX11HandleClipboardEvent(mwinX11Platform* platform, const xcb_generic_event_t* event);

// Fails a read, and drops a reader, past their deadlines.
void mwinX11CheckClipboard(mwinX11Platform* platform, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_X11_CLIPBOARD_H
