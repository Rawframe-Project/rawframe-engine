// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 clipboard.

#include "win32_clipboard.h"

#include "clipboard_data.h"

#include "maul-unicode/encoding.h"

#include <stdckdint.h>
#include <string.h>
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

// The clipboard format of a MIME type: Windows' own names for PNG and
// HTML, else a format registered under the type itself; 0 when none
// could be registered.
static UINT FormatOf(const char* mime, size_t length)
{
    static const wchar_t png[] = L"PNG";
    static const wchar_t html[] = L"HTML Format";
    wchar_t name[MWIN_CLIPBOARD_MIME + 1];
    for (size_t i = 0; i < length; i++)
    {
        name[i] = (wchar_t)mime[i];
    }
    name[length] = 0;
    const wchar_t* registered = mwinSameMime(mime, length, "image/png", 9)   ? png
                                : mwinSameMime(mime, length, "text/html", 9) ? html
                                                                             : name;
    return RegisterClipboardFormatW(registered);
}

// Bytes in memory the clipboard takes; NULL when there is none.
static HGLOBAL Copy(const uint8_t* bytes, size_t length)
{
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, length > 0 ? length : 1);
    uint8_t* copy = memory != nullptr ? GlobalLock(memory) : nullptr;
    if (copy == nullptr)
    {
        return memory != nullptr ? GlobalFree(memory) : nullptr;
    }
    if (length > 0)
    {
        memcpy(copy, bytes, length);
    }
    GlobalUnlock(memory);
    return memory;
}

// Sets memory as a format of the clipboard, which owns it once set:
// false, the memory freed, when it is not.
static bool Set(UINT format, HGLOBAL memory)
{
    bool set = format != 0 && memory != nullptr && SetClipboardData(format, memory) != nullptr;
    if (!set && memory != nullptr)
    {
        GlobalFree(memory);
    }
    return set;
}

mwinOutcome mwinWin32WriteClipboard(const mwinWin32Window* window)
{
    const mwinContext* context = window->platform->context;
    const mwinClipboardCopy* copy = context->clipboardData;
    if (!Open(window->hwnd))
    {
        return mwin_outcomeFailed;
    }
    bool set = EmptyClipboard() &&
               (!mwinOffersClipboardText(context) || Set(CF_UNICODETEXT, Wide(context)));
    for (uint32_t i = 0; set && copy != nullptr && i < copy->count; i++)
    {
        const mwinClipboardDataItem* item = &copy->items[i];
        set = Set(FormatOf(item->mime, item->mimeLength),
                  Copy(mwinClipboardBytesOf(copy, item), item->length));
    }
    CloseClipboard();
    return set ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinOutcome mwinWin32ReadClipboardData(const mwinWin32Window* window, const mwinRequest* request)
{
    mwinContext* context = window->platform->context;
    UINT format = FormatOf(request->value.text.bytes, request->value.text.length);
    if (format == 0 || !Open(window->hwnd))
    {
        return mwin_outcomeFailed;
    }
    HANDLE memory = GetClipboardData(format);
    const uint8_t* bytes = memory != nullptr ? GlobalLock(memory) : nullptr;
    mwinOutcome outcome = mwin_outcomeFailed;
    if (bytes != nullptr)
    {
        // The memory's size, which may run past the bytes put there.
        outcome = mwinTakeClipboardData(context, bytes, GlobalSize(memory));
        GlobalUnlock(memory);
    }
    CloseClipboard();
    return outcome;
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
