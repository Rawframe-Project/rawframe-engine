// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland windows: an xdg_toplevel on a wl_surface. The compositor
// decides a toplevel's state in configure sequences; the first makes
// the window, and a mode request is answered by the next. With the
// viewporter the surface's logical size is its viewport's destination
// and its buffer is in pixels at any scale, fractional ones included;
// without it the scale is the integer buffer scale.

#ifndef MAUL_WINDOW_SRC_WAYLAND_WINDOW_H
#define MAUL_WINDOW_SRC_WAYLAND_WINDOW_H

#include "wayland.h"

// The backend's createWindow, destroyWindow and submit.
void mwinWaylandCreateWindow(mwinContext* context, uint32_t slot);
void mwinWaylandDestroyWindow(mwinContext* context, uint32_t slot);
void mwinWaylandSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_WAYLAND_WINDOW_H
