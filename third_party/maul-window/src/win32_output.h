// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 monitors: each HMONITOR EnumDisplayMonitors lists, with its
// bounds and work area, whether it is primary, its scale from its DPI
// and its refresh rate. WM_DISPLAYCHANGE reads them again.

#ifndef MAUL_WINDOW_SRC_WIN32_OUTPUT_H
#define MAUL_WINDOW_SRC_WIN32_OUTPUT_H

#include "win32.h"

// Reads the monitors again, adding, changing and removing theirs.
void mwinWin32RefreshMonitors(mwinWin32Platform* platform);

// The monitor slot of an HMONITOR, or -1.
int32_t mwinWin32MonitorOf(const mwinWin32Platform* platform, HMONITOR handle);

#endif // MAUL_WINDOW_SRC_WIN32_OUTPUT_H
