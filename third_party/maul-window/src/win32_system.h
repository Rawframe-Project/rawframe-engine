// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts on Win32: the apps' light or dark
// theme, the accent color and the text scale from the user's registry,
// reduced motion from the client area animation setting, the power
// source and battery saver, and the preferred UI languages. Windows
// announces changes to top-level windows; any of those messages reads
// everything again, and the core posts what changed. Window frames
// follow the theme, as Windows' own apps do.

#ifndef MAUL_WINDOW_SRC_WIN32_SYSTEM_H
#define MAUL_WINDOW_SRC_WIN32_SYSTEM_H

#include "win32.h"

// Reads the facts and the locales into the context.
void mwinWin32ReadSystem(mwinWin32Platform* platform);

// Gives a window's frame the theme's look: a dark title bar in a dark
// theme.
void mwinWin32ApplyTheme(const mwinWin32Window* window);

// Whether a message of a window's announces a change of the facts.
bool mwinWin32IsSystemChange(UINT message, WPARAM wParam);

#endif // MAUL_WINDOW_SRC_WIN32_SYSTEM_H
