// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web input: keys by KeyboardEvent.code, the same on every layout, and
// what they mean from the Keyboard API's layout map where the browser
// has one, from KeyboardEvent.key otherwise; text from the keys that
// type a character. The mouse, touch and pens come as pointer events,
// the wheel in detents. A captured cursor is a pointer lock, which the
// browser grants after a user's gesture and the user can end with
// Escape; a page cannot confine the pointer. Browser shortcuts (with
// Control or Meta, F5, F11, F12) stay the browser's, and the chords the
// browser and the system keep are told by mwinWebKeyReach.

#ifndef MAUL_WINDOW_SRC_WEB_INPUT_H
#define MAUL_WINDOW_SRC_WEB_INPUT_H

#include "web.h"

// Sets up and takes down the context's input on the page, and a
// canvas's.
void mwinWebAttachInput(const mwinContext* context);
void mwinWebWatchCanvas(const mwinContext* context, uint32_t slot);

// Handles an input record.
void mwinWebHandleInputRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

// Carries out a cursor mode request: its outcome, or -1 when the page
// answers later.
int mwinWebSetCursorMode(mwinWebPlatform* platform, uint32_t slot, mwinCursorMode mode);

// The backend's mapKeyCode.
mwinKey mwinWebMapKeyCode(const mwinContext* context, mwinKeyCode code);

// Reads which browser family runs the page, Chromium or another, and
// the system under it, for mwinWebKeyReach.
uint8_t mwinWebReadKeyHost(void);

// The backend's keyReach: the browser's answer or the system's, which
// ever keeps the chord more.
mwinKeyReach mwinWebKeyReach(const mwinContext* context, mwinKeyCode code, mwinModifiers modifiers);

#endif // MAUL_WINDOW_SRC_WEB_INPUT_H
