// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland window icons through xdg-toplevel-icon-v1: every image in a
// square shared-memory buffer of its own, its alpha premultiplied as
// Wayland's buffers have it, one not square centred on a clear square.
// The icon applies at the surface's next commit, which setting it makes.

#ifndef MAUL_WINDOW_SRC_WAYLAND_ICON_H
#define MAUL_WINDOW_SRC_WAYLAND_ICON_H

#include "wayland.h"

// Sets the window's icon from a request's images; none gives back the
// compositor's own. Unsupported without the protocol.
mwinOutcome mwinWaylandSetIcon(mwinWaylandWindow* window, const mwinRequest* request);

#endif // MAUL_WINDOW_SRC_WAYLAND_ICON_H
