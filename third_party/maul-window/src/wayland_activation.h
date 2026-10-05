// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus requests through xdg-activation (mwin-0019): a token
// asked for with the latest input serial and the surface that has the
// keyboard, and the window activated with it once it comes. The first
// window shown activates itself with the launcher's token.

#ifndef MAUL_WINDOW_SRC_WAYLAND_ACTIVATION_H
#define MAUL_WINDOW_SRC_WAYLAND_ACTIVATION_H

#include "wayland.h"

// Carries out a focus request: -1 while the token comes, else the
// outcome.
int mwinWaylandRequestFocus(mwinWaylandWindow* window, uint32_t request);

// Activates a window with XDG_ACTIVATION_TOKEN, once for the program,
// and removes the variable so that programs it starts do not take it.
void mwinWaylandActivateAtStart(mwinWaylandWindow* window);

// Lets go of a token still coming.
void mwinWaylandDropActivation(mwinWaylandWindow* window);

#endif // MAUL_WINDOW_SRC_WAYLAND_ACTIVATION_H
