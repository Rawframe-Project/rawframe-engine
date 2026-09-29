// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 touch and pen, from the WM_POINTER messages. The mouse keeps its
// own messages. Touch and pen messages are handled here and never reach
// DefWindowProc, so Windows makes no mouse input of them; a program sees
// a touch as a touch. A touch that Windows takes, for a gesture or
// another window's capture, is cancelled.

#ifndef MAUL_WINDOW_SRC_WIN32_POINTER_H
#define MAUL_WINDOW_SRC_WIN32_POINTER_H

#include "win32.h"

// Handles a touch or pen message of a window: true when handled.
bool mwinWin32HandlePointer(mwinWin32Window* window, UINT message, WPARAM wParam);

#endif // MAUL_WINDOW_SRC_WIN32_POINTER_H
