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

// The monitor's name from a display device's description of capacity
// units: as UTF-8, cut to the most whole characters that fit.
void mwinWin32NameMonitor(const WCHAR* description, size_t capacity, mwinMonitorInfo* info);

// A DisplayConfig refresh rate in millihertz, rounded; 0 for none.
uint32_t mwinWin32RefreshOf(DISPLAYCONFIG_RATIONAL rate);

// The headroom of HDR facts whose other fields are read: 1 where the
// output is SDR, the peak over SDR white (at least 1) where it is HDR
// and both are known, 0 where nothing is known.
void mwinWin32SettleHeadroom(mwinHdrFacts* hdr);

// A refresh in three steps: every monitor marked unseen; each monitor
// Windows lists seen, added the first time and changed when its facts
// differ (left out when every slot is taken); then the monitors not seen
// removed.
void mwinWin32BeginMonitors(mwinWin32Platform* platform);
void mwinWin32SeeMonitor(mwinWin32Platform* platform, HMONITOR handle, const mwinMonitorInfo* info);
void mwinWin32EndMonitors(mwinWin32Platform* platform);

// The monitor slot of an HMONITOR, or -1.
int32_t mwinWin32MonitorOf(const mwinWin32Platform* platform, HMONITOR handle);

#endif // MAUL_WINDOW_SRC_WIN32_OUTPUT_H
