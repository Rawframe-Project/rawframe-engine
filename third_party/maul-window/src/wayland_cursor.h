// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over Wayland windows. Each window keeps the cursor mode and
// shape the program asked for; the pointer shows them when it enters
// the window, or at once when it is there. Shapes come from the cursor
// shape protocol, or else from the cursor theme through
// libwayland-cursor (XCURSOR_THEME and XCURSOR_SIZE). A captured cursor
// is a locked pointer whose relative motion arrives as raw deltas; a
// confined one is a confined pointer. Constraints are persistent: the
// compositor applies them again each time the pointer comes back.

#ifndef MAUL_WINDOW_SRC_WAYLAND_CURSOR_H
#define MAUL_WINDOW_SRC_WAYLAND_CURSOR_H

#include "wayland.h"

// The pointer came, or goes: its shape device, relative motion and the
// windows' constraints.
void mwinWaylandAttachCursors(mwinWaylandPlatform* platform);
void mwinWaylandDetachCursors(mwinWaylandPlatform* platform);

// Shows the cursor of the window under the pointer.
void mwinWaylandShowCursor(mwinWaylandPlatform* platform);

// Shows a shape over the frame of the window in a slot, whatever the
// window's own cursor is.
void mwinWaylandShowFrameCursor(mwinWaylandPlatform* platform, uint32_t slot,
                                mwinCursorShape shape);

// Carries out a cursor mode or shape request of the window in a slot:
// its outcome.
mwinOutcome mwinWaylandSetCursorMode(mwinWaylandPlatform* platform, uint32_t slot,
                                     mwinCursorMode mode);
mwinOutcome mwinWaylandSetCursorShape(mwinWaylandPlatform* platform, uint32_t slot,
                                      mwinCursorShape shape);

// The window in a slot goes: its constraint goes with it.
void mwinWaylandDropCursor(mwinWaylandPlatform* platform, uint32_t slot);

// Frees the cursor theme and its surface, at the end.
void mwinWaylandReleaseCursorTheme(mwinWaylandPlatform* platform);

#endif // MAUL_WINDOW_SRC_WAYLAND_CURSOR_H
