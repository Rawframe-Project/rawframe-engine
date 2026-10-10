// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 services.

#include "win32_services.h"

#include "maul-unicode/encoding.h"
#include "maul-window/services.h"

#include <shellapi.h>
#include <shlobj.h>

// The request's text as UTF-16 and a terminator; paths take Windows'
// separator, which the shell's parsing needs.
static bool Wide(const mwinRequest* request, WCHAR* units, bool path)
{
    size_t count = 0;
    muniTextResult result =
        muniConvertUtf8ToUtf16(request->value.text.bytes, request->value.text.length,
                               muni_convertStrict, (uint16_t*)units, MWIN_ADDRESS_BYTES, &count);
    if (result.status != muni_success)
    {
        return false;
    }
    units[count] = L'\0';
    for (size_t i = 0; path && i < count; i++)
    {
        units[i] = units[i] == L'/' ? L'\\' : units[i];
    }
    return true;
}

mwinOutcome mwinWin32OpenUrl(const mwinWin32Window* window, const mwinRequest* request)
{
    WCHAR address[MWIN_ADDRESS_BYTES + 1];
    if (!Wide(request, address, false))
    {
        return mwin_outcomeFailed;
    }
    // No error box of the shell's: the completion says it failed.
    SHELLEXECUTEINFOW info = {
        .cbSize = sizeof(info),
        .fMask = SEE_MASK_FLAG_NO_UI,
        .hwnd = window->hwnd,
        .lpVerb = L"open",
        .lpFile = address,
        .nShow = SW_SHOWNORMAL,
    };
    return ShellExecuteExW(&info) ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinOutcome mwinWin32RevealFile(const mwinRequest* request)
{
    WCHAR path[MWIN_ADDRESS_BYTES + 1];
    PIDLIST_ABSOLUTE item = nullptr;
    if (!Wide(request, path, true) || FAILED(SHParseDisplayName(path, nullptr, &item, 0, nullptr)))
    {
        return mwin_outcomeFailed;
    }
    // With no children named, Explorer opens the item's folder and
    // selects it.
    HRESULT shown = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    return SUCCEEDED(shown) ? mwin_outcomeDone : mwin_outcomeFailed;
}

void mwinWin32KeepAwake(mwinWin32Platform* platform, bool wanted)
{
    if (wanted != platform->awake)
    {
        platform->awake = wanted;
        (void)SetThreadExecutionState(
            wanted ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
    }
}
