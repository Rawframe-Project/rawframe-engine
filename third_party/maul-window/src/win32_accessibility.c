// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 accessibility.

#include "win32_accessibility.h"

#include "accessibility.h"

// UiaRootObjectId of uiautomationcoreapi.h: the object id of a window's
// UI Automation root.
#define ROOT_OBJECT_ID (-25)

// UiaReturnRawElementProvider, loaded from uiautomationcore.dll.
static bool Load(mwinWin32Platform* platform)
{
    if (platform->uiaReturn == nullptr && !platform->uiaTried)
    {
        platform->uiaTried = true;
        HMODULE library = LoadLibraryW(L"uiautomationcore.dll");
        FARPROC found =
            library != nullptr ? GetProcAddress(library, "UiaReturnRawElementProvider") : nullptr;
        memcpy((void*)&platform->uiaReturn, (const void*)&found, sizeof(platform->uiaReturn));
    }
    return platform->uiaReturn != nullptr;
}

bool mwinWin32AnswerObject(mwinWin32Window* window, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    if ((LONG)lParam != ROOT_OBJECT_ID)
    {
        return false;
    }
    mwinWin32Platform* platform = window->platform;
    mwinNoteAccessibilityAsked(platform->context, window->slot);
    void* root = platform->context->windows[window->slot].accessibilityRoot;
    if (root == nullptr || !Load(platform))
    {
        return false;
    }
    window->uiaAnswered = true;
    *result = platform->uiaReturn(window->hwnd, wParam, lParam, root);
    return true;
}

void mwinWin32ForgetObject(const mwinWin32Window* window)
{
    if (window->uiaAnswered)
    {
        (void)window->platform->uiaReturn(window->hwnd, 0, 0, nullptr);
    }
}
