// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 file dialogs: the common item dialog (IFileOpenDialog,
// IFileSaveDialog). Its Show runs a modal loop of its own until the
// user is done, so a dialog is never shown while the program's frame
// runs: its request waits for the next pump, which shows it, and
// frames go on from a timer on the owner inside the dialog's loop, as
// they do while a window is moved or sized. A dialog whose request goes
// (its window destroyed, a later dialog) or whose program stops is
// closed from that timer.

#ifndef MAUL_WINDOW_SRC_WIN32_DIALOG_H
#define MAUL_WINDOW_SRC_WIN32_DIALOG_H

#include "win32.h"

// The owner's timer while a dialog shows.
#define MWIN_WIN32_DIALOG_TIMER 2

// Takes a window's dialog request, shown at the next pump: -1.
int mwinWin32AskDialog(mwinWin32Window* window);

// Shows a dialog waiting, if one is, until the user is done, and
// answers it; each pump calls it.
void mwinWin32ShowDialogs(mwinWin32Platform* platform);

// The owner's timer while a dialog shows: a frame, and the dialog
// closed when its request went or the program stops.
void mwinWin32DialogTick(mwinWin32Platform* platform);

#endif // MAUL_WINDOW_SRC_WIN32_DIALOG_H
