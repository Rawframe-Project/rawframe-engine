// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on Wayland, through text-input-v3. A window that asks
// for text input is enabled whenever the seat's text input focuses on
// it, with its caret rectangle. The input method's strings wait for its
// done event, which applies them in the protocol's order: the committed
// text as mwin_eventTextInput, then the new composition, one underlined
// segment, as mwin_eventImePreedit; a done without one ends the
// composition.

#ifndef MAUL_WINDOW_SRC_WAYLAND_TEXT_H
#define MAUL_WINDOW_SRC_WAYLAND_TEXT_H

#include "wayland.h"

// Makes the seat's text input where the compositor has the manager, and
// destroys it.
void mwinWaylandAttachText(mwinWaylandPlatform* platform);
void mwinWaylandDetachText(mwinWaylandPlatform* platform);

// Carries out a text input request of the window in a slot.
mwinOutcome mwinWaylandSetTextInput(mwinWaylandPlatform* platform, uint32_t slot, bool enabled,
                                    mwinRect caret);

// The window in a slot goes.
void mwinWaylandForgetTextFocus(mwinWaylandPlatform* platform, uint32_t slot);

#endif // MAUL_WINDOW_SRC_WAYLAND_TEXT_H
