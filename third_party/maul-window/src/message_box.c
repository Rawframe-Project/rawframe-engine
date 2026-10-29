// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box: its def, checked before the platform shows it.

#include "message_box.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static bool IsText(const char* text, size_t length, size_t limit)
{
    return (text != nullptr || length == 0) && length <= limit &&
           muniValidateUtf8(text, length).status == muni_success &&
           (length == 0 || memchr(text, '\0', length) == nullptr);
}

#if !defined(MAUL_WINDOW_WIN32) && !defined(MAUL_WINDOW_WEB) && !defined(MAUL_WINDOW_WAYLAND) &&   \
    !defined(MAUL_WINDOW_X11) && !defined(MAUL_WINDOW_MACOS) && !defined(MAUL_WINDOW_IOS)
// A build with no platform of its own (the test backend alone).
mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    (void)def;
    (void)acceptedOut;
    return mwin_errorUnsupported;
}
#endif

mwinResult mwinShowMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    // The cookie and the version come first in every version's layout.
    if (def != nullptr && def->cookie == MWIN_MESSAGE_BOX_DEF_COOKIE &&
        def->version != MWIN_ABI_VERSION)
    {
        return mwin_errorVersion;
    }
    if (def == nullptr || def->cookie != MWIN_MESSAGE_BOX_DEF_COOKIE ||
        def->kind > mwin_messageError || def->buttons > mwin_buttonsYesNo ||
        !IsText(def->title, def->titleLength, MWIN_MESSAGE_TITLE_BYTES) ||
        !IsText(def->message, def->messageLength, MWIN_MESSAGE_BYTES))
    {
        return mwin_errorInvalid;
    }
    bool accepted = false;
    mwinResult status = mwinPlatformMessageBox(def, &accepted);
    if (status == mwin_success && acceptedOut != nullptr)
    {
        *acceptedOut = accepted;
    }
    return status;
}
