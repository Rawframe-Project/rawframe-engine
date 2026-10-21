// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box drawn by the library itself on an X server, where
// neither zenity nor kdialog is there to show one.

#ifndef MAUL_WINDOW_SRC_X11_MESSAGE_BOX_H
#define MAUL_WINDOW_SRC_X11_MESSAGE_BOX_H

#include "maul-window/services.h"

// Shows a checked def in a window of its own on the display DISPLAY
// names and waits: its title on the window, the message in the server's
// "fixed" font (a character it has not, past Latin-1, as '?'), and its
// buttons. A click on the first button, or Return, accepts; a click on
// the second, Escape, or closing the window refuses. mwin_errorUnsupported
// where no X server answers.
mwinResult mwinX11MessageBox(const mwinMessageBoxDef* def, bool* acceptedOut);

#endif // MAUL_WINDOW_SRC_X11_MESSAGE_BOX_H
