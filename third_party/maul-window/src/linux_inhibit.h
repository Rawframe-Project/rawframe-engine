// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keeping the display awake over the session bus, where no compositor
// protocol does it (X11, and Wayland compositors without idle
// inhibiting): org.freedesktop.ScreenSaver's Inhibit, which KDE, Xfce,
// Cinnamon, MATE and xscreensaver answer, else the desktop portal's
// Inhibit, which GNOME and sandboxes answer. It is one wish of the
// whole program, held while some window that asks for it shows.

#ifndef MAUL_WINDOW_SRC_LINUX_INHIBIT_H
#define MAUL_WINDOW_SRC_LINUX_INHIBIT_H

#include "linux_bus.h"

// The most bytes of the portal's handle for an inhibition.
#define MWIN_INHIBIT_HANDLE_BYTES 256

typedef enum mwinInhibitBy : uint8_t
{
    mwin_inhibitNone,
    mwin_inhibitScreenSaver,
    mwin_inhibitPortal,
} mwinInhibitBy;

typedef struct mwinLinuxInhibit
{
    mwinBusCall call;
    // Whom a call asks, and who holds the display awake.
    mwinInhibitBy asking;
    mwinInhibitBy held;
    uint32_t cookie;
    char handle[MWIN_INHIBIT_HANDLE_BYTES];
    bool wanted;
    // Neither would: not asked again until the wish changes.
    bool refused;
} mwinLinuxInhibit;

// Follows the wish: asks for the display, lets it go, and reads the
// answers; each pump calls it.
void mwinInhibitPump(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit, bool wanted, uint64_t nowNs);

// Lets the display go, and the call on its way.
void mwinInhibitStop(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit);

#endif // MAUL_WINDOW_SRC_LINUX_INHIBIT_H
