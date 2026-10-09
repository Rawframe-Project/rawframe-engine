// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 clipboard and primary selection (mwin-0029): CLIPBOARD and
// PRIMARY, owned and received by a hidden window of the backend's,
// owned at the time of the last input event. As an owner the backend
// answers other clients' requests for TARGETS, TIMESTAMP, UTF8_STRING
// and text/plain;charset=utf-8, and for CLIPBOARD the MIME types of the
// data written, each its own atom, in pieces (INCR) past 64 KiB
// (x11_clipboard.c). A read converts the selection to UTF8_STRING, or a
// data read to its type's atom, and takes the bytes whole or in pieces;
// one read runs at a time, the others waiting their turn
// (x11_clipboard_read.c).

#ifndef MAUL_WINDOW_SRC_X11_CLIPBOARD_H
#define MAUL_WINDOW_SRC_X11_CLIPBOARD_H

#include "x11.h"

// Gives back what the clipboard holds and its hidden window.
void mwinX11StopClipboard(mwinX11Platform* platform);

// Carries out a clipboard or primary selection write or read: an
// outcome, or -1 for a read answered later.
int mwinX11WriteSelection(mwinX11Platform* platform, mwinRequestKind kind);
int mwinX11ReadSelection(mwinX11Platform* platform, const mwinRequest* request);

// Handles an event when it is the clipboard's: true when it was.
bool mwinX11HandleClipboardEvent(mwinX11Platform* platform, const xcb_generic_event_t* event);

// Fails a read past its deadline, and drops readers that stopped.
void mwinX11CheckClipboard(mwinX11Platform* platform, uint64_t nowNs);

// Between the two files: makes the hidden window (false when the X
// server refused); the atom of a selection; ends a read, its bytes given
// back; and the read's part of the events, the owner's answer and a
// property of the hidden window (x11_clipboard_read.c).
bool mwinX11EnsureClipboardWindow(mwinX11Platform* platform);
xcb_atom_t mwinX11SelectionAtom(const mwinX11Platform* platform, int selection);
void mwinX11EndRead(mwinX11Platform* platform);
void mwinX11OnReadNotify(mwinX11Platform* platform, const xcb_selection_notify_event_t* notify);
void mwinX11OnReadProperty(mwinX11Platform* platform, const xcb_property_notify_event_t* event);
void mwinX11CheckRead(mwinX11Platform* platform, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_X11_CLIPBOARD_H
