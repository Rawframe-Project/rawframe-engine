// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux file dialogs through the desktop portal's FileChooser, over the
// session bus: OpenFile or SaveFile, answered by a Response signal on
// the request the call names, whenever the user is done. A handle
// token names the request before the call, so an answer that comes at
// once is not missed. Where there is no bus or no portal, zenity
// (linux_zenity.h). A dialog whose request goes (its window destroyed,
// or a later dialog) is closed.

#ifndef MAUL_WINDOW_SRC_LINUX_DIALOG_H
#define MAUL_WINDOW_SRC_LINUX_DIALOG_H

#include "linux_answer.h"
#include "linux_bus.h"
#include "linux_zenity.h"

// Dialogs open at once, and the bytes of a portal request's path.
#define MWIN_LINUX_DIALOGS 4
#define MWIN_HANDLE_BYTES  192

typedef struct mwinLinuxDialog
{
    mwinServiceAnswer to;
    // The portal's call until it answers, and the request its Response
    // comes on; empty for none.
    mwinBusCall call;
    char handle[MWIN_HANDLE_BYTES];
    mwinZenity zenity;
} mwinLinuxDialog;

typedef struct mwinLinuxDialogs
{
    mwinContext* context;
    mwinLinuxBus* bus;
    mwinLinuxDialog dialogs[MWIN_LINUX_DIALOGS];
    uint32_t tokens;
    // The bus hands this module its signals.
    bool listening;
} mwinLinuxDialogs;

void mwinDialogsStart(mwinLinuxDialogs* dialogs, mwinContext* context, mwinLinuxBus* bus);

// Opens the dialog of a request, over a parent the portal knows ("x11:"
// and the window's id, or empty): -1 while it shows, or the outcome.
int mwinDialogsOpen(mwinLinuxDialogs* dialogs, uint32_t slot, uint32_t request, const char* parent);

// Answers dialogs done, and closes those whose request went; each pump
// calls it, after the bus is read.
void mwinDialogsPump(mwinLinuxDialogs* dialogs, uint64_t nowNs);

// Closes every dialog, before the bus goes.
void mwinDialogsStop(mwinLinuxDialogs* dialogs);

#endif // MAUL_WINDOW_SRC_LINUX_DIALOG_H
