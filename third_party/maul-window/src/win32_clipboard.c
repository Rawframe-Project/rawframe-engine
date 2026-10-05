// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 clipboard.

#include "win32_clipboard.h"

#include "maul-unicode/encoding.h"

#include <stdckdint.h>
#include <wchar.h>

#define OPEN_TRIES 5

static bool Open(HWND hwnd)
{
    for (int i = 0; i < OPEN_TRIES; i++)
    {
        if (OpenClipboard(hwnd))
        {
            return true;
        }
        Sleep(1);
    }
    return false;
}

// The written text as UTF-16 and its terminator, in memory the
// clipboard takes; NULL when there is none.
static HGLOBAL Wide(const mwinContext* context)
{
    size_t needed = 0;
    (void)muniConvertUtf8ToUtf16(context->clipboardOffer, context->clipboardOfferLength, nullptr, 0,
                                 muni_convertStrict, &needed);
    size_t bytes = 0;
    if (ckd_add(&bytes, needed, 1) || ckd_mul(&bytes, bytes, sizeof(uint16_t)))
    {
        return nullptr;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    uint16_t* units = memory != nullptr ? GlobalLock(memory) : nullptr;
    if (units == nullptr)
    {
        return memory != nullptr ? GlobalFree(memory) : nullptr;
    }
    (void)muniConvertUtf8ToUtf16(context->clipboardOffer, context->clipboardOfferLength, units,
                                 needed, muni_convertStrict, &needed);
    units[needed] = 0;
    GlobalUnlock(memory);
    return memory;
}

mwinOutcome mwinWin32WriteClipboard(const mwinWin32Window* window)
{
    HGLOBAL memory = Wide(window->platform->context);
    if (memory == nullptr)
    {
        return mwin_outcomeFailed;
    }
    if (!Open(window->hwnd))
    {
        GlobalFree(memory);
        return mwin_outcomeFailed;
    }
    // The clipboard owns the memory once it is set.
    bool set = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!set)
    {
        GlobalFree(memory);
    }
    CloseClipboard();
    return set ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinOutcome mwinWin32ReadClipboard(const mwinWin32Window* window)
{
    mwinContext* context = window->platform->context;
    if (!Open(window->hwnd))
    {
        return mwin_outcomeFailed;
    }
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    const wchar_t* units = memory != nullptr ? GlobalLock(memory) : nullptr;
    mwinOutcome outcome = mwin_outcomeDone;
    if (units == nullptr)
    {
        // No text, as a program that put only an image leaves it.
        outcome = mwinTakeClipboardUtf16(context, nullptr, 0);
    }
    else
    {
        // Another program's memory need not end with a terminator.
        size_t length = wcsnlen(units, GlobalSize(memory) / sizeof(wchar_t));
        outcome = mwinTakeClipboardUtf16(context, (const uint16_t*)units, length);
        GlobalUnlock(memory);
    }
    CloseClipboard();
    return outcome;
}
