// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tablets on Wayland (mwin-0037): the first seat's tablet seat, whose
// tools post pen records and no mouse record, as a compositor emulates
// the pointer only for a client without the tablet seat. A tool's
// changes gather until its frame event, which posts its barrel button,
// then its contact or its motion. Tablets are kept until removed, as a
// compositor tells a tool near a surface only to a client that keeps its
// tablet; pads are let go of as they come.

#ifndef MAUL_WINDOW_SRC_WAYLAND_TABLET_H
#define MAUL_WINDOW_SRC_WAYLAND_TABLET_H

#include "wayland.h"

// Takes the first seat's tablet seat once both it and the tablet manager
// are bound, whichever comes last.
void mwinWaylandAttachTablets(mwinWaylandPlatform* platform);

// Lets go of the tools and the tablet seat: the seat goes, or the
// connection ends.
void mwinWaylandReleaseTablets(mwinWaylandPlatform* platform);

// The window in a slot goes: tools near it are near nothing.
void mwinWaylandForgetToolFocus(mwinWaylandPlatform* platform, uint32_t slot);

#endif // MAUL_WINDOW_SRC_WAYLAND_TABLET_H
