// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility hooks: the root a window hands the platform, kept in the
// core window once the platform took it, and the notice that a client
// asked for a window's tree, posted once a window.

#ifndef MAUL_WINDOW_SRC_ACCESSIBILITY_H
#define MAUL_WINDOW_SRC_ACCESSIBILITY_H

#include "core.h"

// A client asked the window for its tree: the program is told the first
// time.
void mwinNoteAccessibilityAsked(mwinContext* context, uint32_t slot);

#endif // MAUL_WINDOW_SRC_ACCESSIBILITY_H
