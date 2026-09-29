// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland drag and drop, on the seat's data device: a drag over one of
// the program's windows that offers files (text/uri-list) or text is
// accepted with the copy action and reported; a drop reads both
// through pipes, a piece at each pump, then delivers them and tells
// the source it finished. A drag of neither is refused, and one over a
// frame the backend draws is not the window's.

#ifndef MAUL_WINDOW_SRC_WAYLAND_DROP_H
#define MAUL_WINDOW_SRC_WAYLAND_DROP_H

#include "wayland.h"

// Marks the drag's pipes closed; the backend's start calls it.
void mwinWaylandInitDrag(mwinWaylandDrag* drag);

// The data device's drag events. The offer's facts come from the offer
// events before the enter: whether it has files, and its best text
// type, or NULL.
void mwinWaylandDragEnter(mwinWaylandPlatform* platform, uint32_t serial,
                          struct wl_surface* surface, mwinPosition position,
                          struct wl_data_offer* offer, bool files, const char* textType);
void mwinWaylandDragMotion(mwinWaylandPlatform* platform, mwinPosition position);
void mwinWaylandDragLeave(mwinWaylandPlatform* platform);
void mwinWaylandDragDrop(mwinWaylandPlatform* platform);

// Moves a drop's reads on, and delivers it when they end.
void mwinWaylandPumpDrag(mwinWaylandPlatform* platform, uint64_t nowNs);

// Lets a drag and a drop go, as the seat does.
void mwinWaylandEndDrag(mwinWaylandPlatform* platform);

#endif // MAUL_WINDOW_SRC_WAYLAND_DROP_H
