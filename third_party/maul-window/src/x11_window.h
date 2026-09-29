// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 windows, with the ICCCM and EWMH properties a window manager
// reads. The X server reports sizes and places in ConfigureNotify, and
// the window manager the modes in _NET_WM_STATE; a mode request waits
// for it, half a second at most, and without a window manager is
// answered unsupported. Sizes are logical: pixels over the Xft.dpi
// scale.

#ifndef MAUL_WINDOW_SRC_X11_WINDOW_H
#define MAUL_WINDOW_SRC_X11_WINDOW_H

#include "x11.h"

// The backend's createWindow, destroyWindow and submit.
void mwinX11CreateWindow(mwinContext* context, uint32_t slot);
void mwinX11DestroyWindow(mwinContext* context, uint32_t slot);
void mwinX11Submit(mwinContext* context, uint32_t slot, uint32_t request);

// Handles an event about a window: false for one of another kind.
bool mwinX11HandleWindowEvent(mwinX11Platform* platform, const xcb_generic_event_t* event);

// Answers the mode requests the window manager left unanswered past
// their deadline.
void mwinX11CheckDeadlines(mwinX11Platform* platform);

#endif // MAUL_WINDOW_SRC_X11_WINDOW_H
