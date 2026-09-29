// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 window icons: _NET_WM_ICON, every image as its width, its height
// and its pixels as ARGB with straight alpha, 32 bits each, for the
// window manager to take the sizes it shows.

#ifndef MAUL_WINDOW_SRC_X11_ICON_H
#define MAUL_WINDOW_SRC_X11_ICON_H

#include "x11.h"

// Sets the window's icon from a request's images; none removes it for
// the window manager's own.
mwinOutcome mwinX11SetIcon(mwinX11Platform* platform, xcb_window_t window,
                           const mwinRequest* request);

#endif // MAUL_WINDOW_SRC_X11_ICON_H
