// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland popups through xdg_popup, in place of a toplevel: placed by a
// positioner at an offset from the corner of the owner's window
// geometry, which holds a frame's caption above the content, and slid or
// flipped by the compositor to stay on the output; the popup's configure
// tells where it went. A menu grabs the seat with the latest input's
// serial, which gives it the keyboard; the compositor ends the grab with
// popup_done, a close request. Moves and resizes reposition the popup,
// from xdg_wm_base version 3.

#ifndef MAUL_WINDOW_SRC_WAYLAND_POPUP_H
#define MAUL_WINDOW_SRC_WAYLAND_POPUP_H

#include "wayland.h"

// Makes the window's xdg_popup against its owner, before the surface's
// first commit.
void mwinWaylandMakePopup(mwinWaylandWindow* window);

// Places the popup again at a position against its owner's content and
// a size, in logical units; the next configure reports it. Unsupported
// before xdg_wm_base version 3.
mwinOutcome mwinWaylandPlacePopup(mwinWaylandWindow* window, mwinPosition position, mwinSize size);

// Reports where the configure being applied put the popup, when that
// changed.
void mwinWaylandSettlePopup(mwinWaylandWindow* window);

void mwinWaylandDestroyPopup(mwinWaylandWindow* window);

#endif // MAUL_WINDOW_SRC_WAYLAND_POPUP_H
