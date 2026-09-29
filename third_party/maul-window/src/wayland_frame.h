// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame the backend draws for a decorated window where the
// compositor draws none (W4): no xdg-decoration, or a compositor that
// chose client-side decorations. A caption above the content moves the
// window and holds close, maximize and minimize buttons drawn as
// shapes, with no title text; invisible margins around it resize the
// window. The parts are subsurfaces from shared memory, drawn in the
// system's theme. The window geometry takes in the caption, so the
// compositor's sizes are the frame's and the content is smaller by the
// caption. A maximized window keeps the caption alone; a full-screen
// one has no frame.

#ifndef MAUL_WINDOW_SRC_WAYLAND_FRAME_H
#define MAUL_WINDOW_SRC_WAYLAND_FRAME_H

#include "wayland.h"

// The height of the caption, in logical units.
#define MWIN_FRAME_CAPTION 28

// The caption height a window has in a mode: 0 without a frame.
int32_t mwinWaylandCaptionOf(const mwinWaylandPlatform* platform, const mwinWaylandWindow* window,
                             mwinWindowMode mode);

// Makes, lays out and draws the frame of the window in a slot for its
// size, scale and mode, or hides it, and sets the window geometry.
void mwinWaylandUpdateFrame(mwinWaylandPlatform* platform, uint32_t slot);

// Destroys the frame of the window in a slot.
void mwinWaylandDestroyFrame(mwinWaylandPlatform* platform, uint32_t slot);

// The pointer on a frame part: false for a surface that is none.
bool mwinWaylandFrameEnter(mwinWaylandPlatform* platform, struct wl_surface* surface,
                           mwinPosition position);
void mwinWaylandFrameMotion(mwinWaylandPlatform* platform, mwinPosition position);
void mwinWaylandFrameButton(mwinWaylandPlatform* platform, uint32_t serial, uint32_t button,
                            bool pressed, uint64_t timeNs);
void mwinWaylandFrameLeave(mwinWaylandPlatform* platform);

// A press on a toplevel's content at a point in logical units, and its
// click count: true when a hit region of custom chrome gave it to the
// compositor, and the program is not told of it. On a caption, the left
// button moves the window, or maximizes or restores it on a second
// click, and the right button opens the window menu; on an edge, the
// left button resizes it.
bool mwinWaylandPressChrome(mwinWaylandPlatform* platform, uint32_t slot, uint32_t serial,
                            uint32_t button, mwinPosition at, uint8_t clicks);

#endif // MAUL_WINDOW_SRC_WAYLAND_FRAME_H
