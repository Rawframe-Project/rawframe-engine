// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland pointer and touch screen. Pointer events are gathered
// into the pointer's frames: a frame posts at most one motion and one
// wheel record. Wheel turns count high-resolution steps where the seat
// sends them, discrete steps where it sends those, and otherwise the
// continuous distance, ten units to a detent. Quick clicks are counted
// by the backend (clicks.h), since Wayland has no double-click setting.

#ifndef MAUL_WINDOW_SRC_WAYLAND_POINTER_H
#define MAUL_WINDOW_SRC_WAYLAND_POINTER_H

#include "wayland.h"

// The seat has a pointer or a touch screen now, or no longer has one.
void mwinWaylandAddPointer(mwinWaylandPlatform* platform);
void mwinWaylandRemovePointer(mwinWaylandPlatform* platform);
void mwinWaylandAddTouch(mwinWaylandPlatform* platform);
void mwinWaylandRemoveTouch(mwinWaylandPlatform* platform);

// The window in a slot goes: the pointer and its touches leave it.
void mwinWaylandForgetPointerFocus(mwinWaylandPlatform* platform, uint32_t slot);

#endif // MAUL_WINDOW_SRC_WAYLAND_POINTER_H
