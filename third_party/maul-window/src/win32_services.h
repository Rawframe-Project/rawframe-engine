// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 services: addresses opened by the shell, files shown by
// Explorer, and the display kept awake by the thread's execution
// state while a window that asks for it shows.

#ifndef MAUL_WINDOW_SRC_WIN32_SERVICES_H
#define MAUL_WINDOW_SRC_WIN32_SERVICES_H

#include "win32.h"

// Opens the request's address in the user's default program.
mwinOutcome mwinWin32OpenUrl(const mwinWin32Window* window, const mwinRequest* request);

// Shows the request's file in Explorer, selected.
mwinOutcome mwinWin32RevealFile(const mwinRequest* request);

// Keeps the display awake while some window that asks for it shows,
// or lets it go; each pump calls it, and the backend's end with false.
void mwinWin32KeepAwake(mwinWin32Platform* platform, bool wanted);

#endif // MAUL_WINDOW_SRC_WIN32_SERVICES_H
