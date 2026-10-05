// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Linux services, for the Wayland and X11 backends alike.
//
// An address goes to xdg-open, which every desktop has and which hands
// it to the desktop's own opener, or to the desktop portal inside a
// sandbox; it runs without a shell, and is answered by how it ends: 0
// done, 3 (no tool for the desktop) unsupported, anything else failed,
// or done when it still runs after a few seconds, showing what it
// opened. A file is shown by the file manager over the session bus
// (org.freedesktop.FileManager1's ShowItems, which selects it), else
// its folder is opened by xdg-open. The display is kept awake over the
// bus too, where the backend has no way of its own (linux_inhibit.h).
// The preferred locales come from the environment (linux_locale.h), the
// look and motion from the desktop portal (linux_settings.h), the power
// from the portal and UPower (linux_power.h).

#ifndef MAUL_WINDOW_SRC_LINUX_SERVICES_H
#define MAUL_WINDOW_SRC_LINUX_SERVICES_H

#include "answer.h"
#include "core.h"
#include "linux_bus.h"
#include "linux_dialog.h"
#include "linux_inhibit.h"
#include "linux_power.h"
#include "linux_settings.h"

#include <sys/types.h>

// The xdg-open runs followed at once, and the file manager's calls.
#define MWIN_LINUX_OPENERS 8
#define MWIN_LINUX_REVEALS 4
// The most of the environment's locales kept, in bytes.
#define MWIN_LINUX_LOCALE_BYTES 256

// An xdg-open run: 0 for none. It is followed until it ends, after its
// answer too, so none is left unreaped.
typedef struct mwinLinuxOpener
{
    pid_t pid;
    mwinServiceAnswer to;
    uint64_t deadlineNs;
} mwinLinuxOpener;

typedef struct mwinLinuxReveal
{
    mwinBusCall call;
    mwinServiceAnswer to;
} mwinLinuxReveal;

typedef struct mwinLinuxServices
{
    mwinContext* context;
    mwinLinuxBus bus;
    mwinLinuxOpener openers[MWIN_LINUX_OPENERS];
    mwinLinuxReveal reveals[MWIN_LINUX_REVEALS];
    mwinLinuxInhibit inhibit;
    mwinLinuxDialogs dialogs;
    mwinLinuxSettings settings;
    mwinLinuxPower power;
} mwinLinuxServices;

void mwinLinuxServicesStart(mwinLinuxServices* services, mwinContext* context);

// Carry out a request of the window: -1 while it is answered later,
// else the outcome.
int mwinLinuxOpenUrl(mwinLinuxServices* services, uint32_t slot, uint32_t request);
int mwinLinuxRevealFile(mwinLinuxServices* services, uint32_t slot, uint32_t request);

// Whether the display can be kept awake over the bus: done, or
// unsupported without a bus.
mwinOutcome mwinLinuxCanKeepAwake(mwinLinuxServices* services);

// Answers what has ended, and keeps the display awake over the bus
// while awake says; each pump calls it.
void mwinLinuxServicesPump(mwinLinuxServices* services, uint64_t nowNs, bool awake);

// Lets everything go; xdg-open runs still going are left to end alone.
void mwinLinuxServicesStop(mwinLinuxServices* services);

#endif // MAUL_WINDOW_SRC_LINUX_SERVICES_H
