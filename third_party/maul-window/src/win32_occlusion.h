// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion on Win32. Windows tells no window that others cover it; it
// tells what hides one wholly: minimizing it, DWM cloaking it (on
// another virtual desktop), the session locked and the display off. A
// window is occluded while any of these holds, and revealed when none
// does. Coverage by other windows can only be computed, by walking every
// window on the desktop at each change, as Chromium does on a thread of
// its own; the library starts no thread, and leaves it out.

#ifndef MAUL_WINDOW_SRC_WIN32_OCCLUSION_H
#define MAUL_WINDOW_SRC_WIN32_OCCLUSION_H

#include "win32.h"

// Listens for cloaking of the thread's windows, and stops.
void mwinWin32StartOcclusion(mwinWin32Platform* platform);
void mwinWin32StopOcclusion(mwinWin32Platform* platform);

// Asks Windows to tell a new window of the session's locking and the
// display's state, reads whether it is minimized or cloaked and whether
// the session is locked now, and posts occluded where it is; lets go of
// what it asked as the window goes.
void mwinWin32WatchOcclusion(mwinWin32Window* window);
void mwinWin32UnwatchOcclusion(mwinWin32Window* window);

// Follows a window's messages that minimize or restore it, lock or
// unlock the session, and turn the display off or on.
void mwinWin32ObserveOcclusion(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam);

#endif // MAUL_WINDOW_SRC_WIN32_OCCLUSION_H
