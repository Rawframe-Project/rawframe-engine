// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over Wayland windows. Each window keeps the cursor mode and
// shape the program asked for; the pointer shows them when it enters
// the window, or at once when it is there, and so does a tablet tool
// that comes near it. Shapes come from the cursor shape protocol, or
// else from the cursor theme through libwayland-cursor (XCURSOR_THEME
// and XCURSOR_SIZE); cursors made from images are shared memory buffers
// on a surface of their own. A captured cursor is a locked pointer
// whose relative motion arrives as raw deltas; a confined one is a
// confined pointer. Constraints are persistent: the compositor applies
// them again each time the pointer comes back.

#ifndef MAUL_WINDOW_SRC_WAYLAND_CURSOR_H
#define MAUL_WINDOW_SRC_WAYLAND_CURSOR_H

#include "wayland.h"

// The pointer came, or goes: its shape device, relative motion and the
// windows' constraints.
void mwinWaylandAttachCursors(mwinWaylandPlatform* platform);
void mwinWaylandDetachCursors(mwinWaylandPlatform* platform);

// Shows the cursor of the window under the pointer.
void mwinWaylandShowCursor(mwinWaylandPlatform* platform);

// Shows the cursor of the window a tablet tool is near: its mode and
// shape; a cursor made from images shows over the pointer only, the
// tool showing the window's last shape.
void mwinWaylandShowToolCursor(mwinWaylandPlatform* platform, mwinWaylandTool* tool);

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

// Carries out a request for a cursor made from images (mwin-0027):
// unsupported without shared memory.
mwinOutcome mwinWaylandSetCursorImage(mwinWaylandPlatform* platform, uint32_t slot,
                                      mwinCursorId cursor);

// The backend's releaseCursor: windows showing the cursor in a slot
// show the default shape, and its buffers are destroyed.
void mwinWaylandReleaseCursor(mwinContext* context, uint32_t slot);

// The window in a slot goes: its constraint goes with it.
void mwinWaylandDropCursor(mwinWaylandPlatform* platform, uint32_t slot);

// Frees the cursor theme and the cursor surfaces, at the end.
void mwinWaylandReleaseCursorTheme(mwinWaylandPlatform* platform);

#endif // MAUL_WINDOW_SRC_WAYLAND_CURSOR_H
