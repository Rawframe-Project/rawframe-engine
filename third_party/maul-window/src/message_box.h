// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box as the platform built for shows it: one of
// win32_message_box.c, macos_message_box.m, web_message_box.c or
// posix_message_box.c, or none, when message_box.c answers that there
// is none.

#ifndef MAUL_WINDOW_SRC_MESSAGE_BOX_H
#define MAUL_WINDOW_SRC_MESSAGE_BOX_H

#include "maul-window/services.h"

// Shows a checked def and waits: as mwinShowMessageBox.
mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut);

#endif // MAUL_WINDOW_SRC_MESSAGE_BOX_H
