// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland seat: the first one the compositor announces, whose
// capabilities add and remove its input devices.

#ifndef MAUL_WINDOW_SRC_WAYLAND_SEAT_H
#define MAUL_WINDOW_SRC_WAYLAND_SEAT_H

#include "wayland.h"

// Binds a seat the registry announced, unless one is bound.
void mwinWaylandBindSeat(mwinWaylandPlatform* platform, uint32_t name, uint32_t version);

// A global went: when it was the seat, its devices go with it.
void mwinWaylandRemoveSeat(mwinWaylandPlatform* platform, uint32_t name);

// Releases the seat and its devices, at the end.
void mwinWaylandReleaseSeat(mwinWaylandPlatform* platform);

#endif // MAUL_WINDOW_SRC_WAYLAND_SEAT_H
