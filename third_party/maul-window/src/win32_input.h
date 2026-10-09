// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 keyboard, mouse and cursor input. Keys are known by scan code,
// the same on every layout; their meaning is what the layout makes of
// them with no modifier, and text comes from WM_CHAR, a character
// outside the BMP in two messages. Menu mode by F10 or Alt alone is
// kept from freezing the loop; Alt+F4 and Alt+Space stay Windows'.
// Quick clicks follow Windows' double-click time and distance. The
// cursor of each window is set in WM_SETCURSOR; a confined cursor is
// clipped to the client area while the window has focus, and a
// captured one is hidden and clipped to its middle, its motion coming
// as raw input.

#ifndef MAUL_WINDOW_SRC_WIN32_INPUT_H
#define MAUL_WINDOW_SRC_WIN32_INPUT_H

#include "win32.h"

// Handles an input message of a window: true when handled, with the
// result in *result.
bool mwinWin32HandleInput(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                          LRESULT* result);

// Carries out a cursor mode or shape request of the window in a slot.
mwinOutcome mwinWin32SetCursorMode(mwinWin32Window* window, mwinCursorMode mode);
mwinOutcome mwinWin32SetCursorShape(mwinWin32Window* window, mwinCursorShape shape);
mwinOutcome mwinWin32SetCursorImage(mwinWin32Window* window, mwinCursorId cursor);

// The cursor made from images the window shows, made at the window's DPI
// the first time; NULL while it shows a shape or the cursor is gone.
HCURSOR mwinWin32CursorOf(const mwinWin32Window* window);

// Destroys the cursors made for the cursor in a slot, and shows the
// default shape over the windows showing it (mwin-0027).
void mwinWin32ReleaseCursor(mwinContext* context, uint32_t slot);

// Clips the cursor as the window's mode wants, now that the window has
// focus or moved; or lets it go.
void mwinWin32ClipCursor(const mwinWin32Window* window, bool focused);

// The backend's mapKeyCode and keyboardLayout.
mwinKey mwinWin32MapKeyCode(mwinKeyCode code);
mwinResult mwinWin32KeyboardLayout(char* buffer, size_t capacity, size_t* lengthOut);

#endif // MAUL_WINDOW_SRC_WIN32_INPUT_H
