// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 monitors, from RandR 1.5's monitor list: each by the atom of its
// name, primary as RandR says or the first where it names none, at the
// scale Xft.dpi gives the whole desktop. Without RandR 1.5 the screen
// is the one monitor.

#ifndef MAUL_WINDOW_SRC_X11_OUTPUT_H
#define MAUL_WINDOW_SRC_X11_OUTPUT_H

#include "x11.h"

// Reads the monitors again, adding, changing and removing theirs.
void mwinX11RefreshMonitors(mwinX11Platform* platform);

// The monitor slot whose bounds hold a point of the desktop, or -1.
int32_t mwinX11MonitorAt(const mwinX11Platform* platform, int32_t x, int32_t y);

#endif // MAUL_WINDOW_SRC_X11_OUTPUT_H
