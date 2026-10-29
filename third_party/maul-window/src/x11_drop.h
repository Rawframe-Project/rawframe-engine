// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 drag and drop by XDND 5: the program's windows say they take
// drops (XdndAware), a drag whose source offers text/uri-list or UTF-8
// text is accepted with the copy action and reported from its first
// position, as XDND's enter carries none; a drop converts XdndSelection
// to the files, then the text, into a property of the window, and
// tells the source it finished. A drag of neither is refused. Text
// sent in pieces (INCR) is left out, the drop marked truncated.

#ifndef MAUL_WINDOW_SRC_X11_DROP_H
#define MAUL_WINDOW_SRC_X11_DROP_H

#include "x11.h"

// Handles an XDND client message, or the answer to a drop's conversion:
// true when it was one.
bool mwinX11HandleDropEvent(mwinX11Platform* platform, const xcb_generic_event_t* event);

// Ends a drop past its deadline.
void mwinX11CheckDrop(mwinX11Platform* platform, uint64_t nowNs);

// Ends the drag over a window that goes, before its X id is let go: a
// drop under way is refused to its source and delivered to no window, so
// a window that takes the slot later never gets it.
void mwinX11ForgetDrag(mwinX11Platform* platform, uint32_t slot);

#endif // MAUL_WINDOW_SRC_X11_DROP_H
