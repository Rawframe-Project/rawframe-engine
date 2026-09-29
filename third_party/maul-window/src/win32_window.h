// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 windows. The process is made per-monitor DPI aware (version 2)
// unless it chose an awareness itself, so each window has its
// monitor's DPI and its client area is sized in pixels for its logical
// size. Windows reports sizes, places, focus and visibility in the
// window procedure's messages, which post the records. Borderless full
// screen is a popup window covering its monitor, back to its old
// placement when it leaves.

#ifndef MAUL_WINDOW_SRC_WIN32_WINDOW_H
#define MAUL_WINDOW_SRC_WIN32_WINDOW_H

#include "win32.h"

// The class name every window of the library shares.
#define MWIN_WIN32_CLASS L"MaulWindow"

// The window procedure of the class.
LRESULT CALLBACK mwinWin32WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

// The backend's createWindow, destroyWindow and submit.
void mwinWin32CreateWindow(mwinContext* context, uint32_t slot);
void mwinWin32DestroyWindow(mwinContext* context, uint32_t slot);
void mwinWin32Submit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_WIN32_WINDOW_H
