// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 window icons: a big one (the taskbar, the switcher) and a small
// one (the title bar), each made from the image nearest the size
// Windows shows at the window's DPI.

#ifndef MAUL_WINDOW_SRC_WIN32_ICON_H
#define MAUL_WINDOW_SRC_WIN32_ICON_H

#include "icon.h"
#include "win32.h"

// Sets the window's icon from a request's images; none gives back the
// class's.
mwinOutcome mwinWin32SetIcon(mwinWin32Window* window, const mwinRequest* request);

// Destroys the icons made for a window, once it is gone.
void mwinWin32ReleaseIcons(mwinWin32Window* window);

// An icon made from an image, or a cursor with its hotspot: NULL when
// Windows refuses.
HICON mwinWin32MakeIcon(const mwinIconCopyImage* image, bool cursor, uint32_t hotspotX,
                        uint32_t hotspotY);

#endif // MAUL_WINDOW_SRC_WIN32_ICON_H
