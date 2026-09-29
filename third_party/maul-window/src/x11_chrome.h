// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Custom chrome on X11. A left press on a caption or an edge region goes
// to the window manager as _NET_WM_MOVERESIZE, once the pointer grab the
// X server took at the press is given up. A second click on a caption
// maximizes or restores the window through _NET_WM_STATE. Messages to
// the window manager go to the root window, where it listens.

#ifndef MAUL_WINDOW_SRC_X11_CHROME_H
#define MAUL_WINDOW_SRC_X11_CHROME_H

#include "x11.h"

// Sends a client message about a window to the root window.
void mwinX11SendToRoot(const mwinX11Platform* platform, xcb_window_t window, xcb_atom_t type,
                       const uint32_t data[5]);

// A left press at a point of a window's client area, in logical units,
// and its click count: true when a hit region gave it to the window
// manager, and the program is not told of it.
bool mwinX11PressChrome(const mwinX11Platform* platform, uint32_t slot,
                        const xcb_button_press_event_t* event, mwinPosition at, uint8_t clicks);

#endif // MAUL_WINDOW_SRC_X11_CHROME_H
