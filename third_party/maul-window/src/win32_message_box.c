// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on Win32: MessageBoxW, task modal and brought to the
// foreground, since it may come before any window.

#include "message_box.h"

#include "maul-unicode/encoding.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Text as UTF-16 with its terminator.
static void Wide(const char* text, size_t length, WCHAR* out, size_t capacity)
{
    size_t units = 0;
    (void)muniConvertUtf8ToUtf16(text, length, (uint16_t*)out, capacity - 1, muni_convertReplace,
                                 &units);
    out[units < capacity ? units : capacity - 1] = L'\0';
}

mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    static const UINT icons[] = {MB_ICONINFORMATION, MB_ICONWARNING, MB_ICONERROR};
    static const UINT buttons[] = {MB_OK, MB_OKCANCEL, MB_YESNO};
    // A UTF-16 unit for each byte of UTF-8 at most.
    WCHAR title[MWIN_MESSAGE_TITLE_BYTES + 1];
    WCHAR text[MWIN_MESSAGE_BYTES + 1];
    Wide(def->title, def->titleLength, title, MWIN_MESSAGE_TITLE_BYTES + 1);
    Wide(def->message, def->messageLength, text, MWIN_MESSAGE_BYTES + 1);
    int answer =
        MessageBoxW(nullptr, text, title,
                    icons[def->kind] | buttons[def->buttons] | MB_TASKMODAL | MB_SETFOREGROUND);
    if (answer == 0)
    {
        return mwin_errorPlatform;
    }
    *acceptedOut = answer == IDOK || answer == IDYES;
    return mwin_success;
}
