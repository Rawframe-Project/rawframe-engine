// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's touch keyboard and input purpose (mwin-0033). The window's
// input scope, set through msctf's SetInputScope, tells the touch
// keyboard and the input method what the field takes; the keyboard is
// shown and hidden through the window's InputPane (IInputPaneInterop,
// Windows 10 1607 and later), which shows it only where no hardware
// keyboard is attached and the window is in the foreground. Where the
// keyboard shows over a window comes from IFrameworkInputPane (Windows
// 8 and later), in screen pixels, which a window advises once it first
// asks for the keyboard.

#ifndef MAUL_WINDOW_SRC_WIN32_TOUCH_KEYBOARD_H
#define MAUL_WINDOW_SRC_WIN32_TOUCH_KEYBOARD_H

#include "win32.h"

// Carries out a virtual keyboard request: done when Windows took it,
// denied when it declined, unsupported without the InputPane.
mwinOutcome mwinWin32SetTouchKeyboard(mwinWin32Window* window, bool visible,
                                      mwinInputPurpose purpose);

// Reports the part of the window the touch keyboard covers, given where
// it is on the screen in pixels, or NULL when it hides.
void mwinWin32CoverWindow(mwinWin32Window* window, const RECT* keyboard);

// Stops following the touch keyboard for a window being destroyed.
void mwinWin32DetachPane(mwinWin32Window* window);

// Frees what the requests loaded.
void mwinWin32StopTouchKeyboard(mwinWin32Platform* platform);

#endif // MAUL_WINDOW_SRC_WIN32_TOUCH_KEYBOARD_H
