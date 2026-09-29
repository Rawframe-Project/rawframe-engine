// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over X11 windows. X11 keeps a cursor per window, so each
// window's cursor is set when the program asks and shows wherever the
// pointer goes over it: a shape from the cursor theme through
// libxcb-cursor, or an empty cursor that hides the pointer. A confined
// cursor is a pointer grab that confines it to the window while the
// window has focus; a captured one is a hidden confined cursor, put in
// the window's middle, whose motion arrives through XInput 2's raw
// events (x11_input.h), and is unsupported without XInput 2.

#ifndef MAUL_WINDOW_SRC_X11_CURSOR_H
#define MAUL_WINDOW_SRC_X11_CURSOR_H

#include "x11.h"

// Makes the empty cursor and the theme context, and frees them.
void mwinX11StartCursors(mwinX11Platform* platform);
void mwinX11StopCursors(mwinX11Platform* platform);

// Carries out a cursor mode or shape request of the window in a slot.
mwinOutcome mwinX11SetCursorMode(mwinX11Platform* platform, uint32_t slot, mwinCursorMode mode);
mwinOutcome mwinX11SetCursorShape(mwinX11Platform* platform, uint32_t slot, mwinCursorShape shape);

// The window in a slot gained or lost focus: a confined cursor is
// grabbed or let go.
void mwinX11CursorFocus(mwinX11Platform* platform, uint32_t slot, bool focused);

#endif // MAUL_WINDOW_SRC_X11_CURSOR_H
