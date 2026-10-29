// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Windows input scope a text field's purpose sets
// (win32_touch_keyboard.c), with no platform type, so that tests run it
// on any platform. The scopes are inputscope.h's numbers: the number,
// email, password and URL purposes have their own, and the text purpose
// and any value the contract does not name take the default one.

#ifndef MAUL_WINDOW_SRC_WIN32_SCOPE_H
#define MAUL_WINDOW_SRC_WIN32_SCOPE_H

#include "maul-window/input.h"

// The input scopes of inputscope.h the purposes map to.
enum
{
    mwin_scopeDefault = 0,
    mwin_scopeUrl = 1,
    mwin_scopeEmail = 5,
    mwin_scopeNumber = 29,
    mwin_scopePassword = 31,
};

// The input scope of a purpose.
int mwinWin32ScopeOf(mwinInputPurpose purpose);

#endif // MAUL_WINDOW_SRC_WIN32_SCOPE_H
