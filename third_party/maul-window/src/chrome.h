// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Custom chrome: the hit regions a program declares, kept in the core
// window as it asks, and the one hit test every backend makes with them.

#ifndef MAUL_WINDOW_SRC_CHROME_H
#define MAUL_WINDOW_SRC_CHROME_H

#include "core.h"

// What a point of the window's client area, in logical units, is: the
// last region holding it, else the client.
mwinHitKind mwinHitAt(const mwinWindow* window, float x, float y);

// Whether a kind starts a move or a resize through the platform.
static inline bool mwinHitMoves(mwinHitKind kind)
{
    return kind >= mwin_hitCaption && kind <= mwin_hitBottomRight;
}

#endif // MAUL_WINDOW_SRC_CHROME_H
