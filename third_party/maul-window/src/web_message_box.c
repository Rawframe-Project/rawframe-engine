// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on the web: alert for OK alone, confirm otherwise,
// the title above the message, as a page's dialogs have no title of
// their own. Without a page there is none.

#include "message_box.h"
#include "web_js.h"

EM_JS_DEPS(mwin_web_message_box, "$UTF8ToString");

// clang-format off
// -1 without a page, else whether it was accepted.
EM_JS(int, mwinWebShowMessage, (const char* title, uint32_t titleLength, const char* text,
                  uint32_t textLength, bool question), {
    if (typeof window === 'undefined' || !window.alert) {
        return -1;
    }
    const heading = UTF8ToString(title, titleLength);
    const body = UTF8ToString(text, textLength);
    const message = heading ? heading + "\n\n" + body : body;
    if (!question) {
        window.alert(message);
        return 1;
    }
    return window.confirm(message) ? 1 : 0;
});
// clang-format on

mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    int answer = mwinWebShowMessage(def->title, (uint32_t)def->titleLength, def->message,
                                    (uint32_t)def->messageLength, def->buttons != mwin_buttonsOk);
    if (answer < 0)
    {
        return mwin_errorUnsupported;
    }
    *acceptedOut = answer == 1;
    return mwin_success;
}
