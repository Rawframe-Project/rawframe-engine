// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 drag and drop: an OLE drop target per window, taking drags
// that carry files (CF_HDROP) or text (CF_UNICODETEXT) with the copy
// effect, and reporting a drag over that has moved. Where OLE cannot
// start on the thread (the program made it multithreaded), files still
// come through WM_DROPFILES, with no drag records.

#ifndef MAUL_WINDOW_SRC_WIN32_DROP_H
#define MAUL_WINDOW_SRC_WIN32_DROP_H

#include "win32.h"

// Starts OLE for the thread, and ends it.
void mwinWin32StartOle(mwinWin32Platform* platform);
void mwinWin32StopOle(mwinWin32Platform* platform);

// Makes a new window a drop target, and stops it being one.
void mwinWin32AttachDrop(mwinWin32Window* window);
void mwinWin32DetachDrop(mwinWin32Window* window);

// Handles WM_DROPFILES: true when it was.
bool mwinWin32HandleDropFiles(mwinWin32Window* window, UINT message, WPARAM wParam);

#endif // MAUL_WINDOW_SRC_WIN32_DROP_H
