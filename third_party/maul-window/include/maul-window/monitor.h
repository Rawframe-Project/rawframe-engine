// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Monitors. Each has an id that stays the same while it is connected;
// mwin_eventMonitorAdded and mwin_eventMonitorRemoved report hotplug,
// mwin_eventMonitorChanged a change of its facts, and a window whose
// monitor changes gets mwin_eventDisplayChanged. Listing the monitors
// gives a consistent snapshot of those connected.

#ifndef MAUL_WINDOW_MONITOR_H
#define MAUL_WINDOW_MONITOR_H

#include "maul-window/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // The most bytes of a monitor's name.
#define MWIN_MONITOR_NAME_BYTES 64

    // A rectangle in the desktop's pixels.
    typedef struct mwinPixelRect
    {
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
    } mwinPixelRect;

    // What a monitor says about high dynamic range. The luminances are in
    // nits, 0 where the platform does not tell.
    typedef struct mwinHdrFacts
    {
        // The platform reports HDR facts for this monitor.
        bool known;
        // HDR output is on.
        bool active;
        float peakNits;
        float fullFrameNits;
        // The luminance the platform shows SDR white at, which SDR content
        // scales to in HDR output.
        float sdrWhiteNits;
    } mwinHdrFacts;

    // What the platform tells about a monitor.
    typedef struct mwinMonitorInfo
    {
        // UTF-8, not NUL-terminated.
        char name[MWIN_MONITOR_NAME_BYTES];
        uint32_t nameLength;
        // Where it is on the desktop and its size, and the part windows
        // may use (without task bars and docks).
        mwinPixelRect bounds;
        mwinPixelRect workArea;
        // Its physical size in millimeters, 0 where unknown.
        uint32_t widthMm;
        uint32_t heightMm;
        float scale;
        // Refresh rate in millihertz (59,940 for 59.94 Hz), 0 where
        // unknown.
        uint32_t refreshMilliHz;
        bool variableRefresh;
        bool primary;
        mwinHdrFacts hdr;
    } mwinMonitorInfo;

    /// Lists the monitors connected now.
    ///
    /// @param context   The context.
    /// @param monitors  Receives up to capacity ids, the primary monitor
    ///                  first. May be NULL when capacity is 0.
    /// @param capacity  The ids monitors holds.
    /// @param countOut  Receives the number of monitors connected, which
    ///                  may exceed capacity.
    /// @return `mwin_success`; `mwin_errorCapacity` when they do not all
    ///         fit (the first capacity are written); `mwin_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetMonitors(const mwinContext* context,
                                                       mwinMonitorId* monitors, size_t capacity,
                                                       size_t* countOut);

    /// Reads what the platform tells about a monitor.
    ///
    /// @param context  The context.
    /// @param monitor  The monitor.
    /// @param infoOut  Receives the facts.
    /// @return `mwin_success`; `mwin_errorStale` for a monitor no longer
    ///         connected; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetMonitorInfo(const mwinContext* context,
                                                          mwinMonitorId monitor,
                                                          mwinMonitorInfo* infoOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_MONITOR_H
