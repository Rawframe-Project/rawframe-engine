// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on Win32, through IMM32 (mwin-0032): every input method
// is a TSF text service, and TSF passes its compositions to a window
// without a text store as IMM32 messages.

#include "win32_ime.h"

#include "maul-unicode/encoding.h"

#include <imm.h>
#include <math.h>

static void Post(mwinWin32Window* window, const mwinEvent* event)
{
    mwinPost(window->platform->context, window->slot, event);
}

static void PostType(mwinWin32Window* window, mwinEventType type)
{
    mwinEvent event = {.type = type, .timeNs = mwinWin32Now()};
    Post(window, &event);
}

// Ends a composition the window shows.
static void EndComposition(mwinWin32Window* window)
{
    if (!window->platform->context->windows[window->slot].state.composing)
    {
        return;
    }
    mwinEvent end = {.type = mwin_eventImePreedit, .timeNs = mwinWin32Now()};
    end.data.preedit.caret = -1;
    Post(window, &end);
}

uint32_t mwinWin32Utf8Before(const WCHAR* units, uint32_t count, uint32_t index)
{
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < index && i < count; i++)
    {
        uint32_t unit = units[i];
        bool pair = unit >= 0xD800 && unit <= 0xDBFF && i + 1 < count && units[i + 1] >= 0xDC00 &&
                    units[i + 1] <= 0xDFFF;
        bytes += unit < 0x80 ? 1 : (unit < 0x800 ? 2 : (pair ? 4 : 3));
        i += pair ? 1 : 0;
    }
    return bytes;
}

// A string of the input context's as UTF-8 in the platform's buffer:
// its units, or 0 when it is empty or does not fit.
static uint32_t ReadString(mwinWin32Window* window, HIMC context, DWORD which, uint32_t* lengthOut)
{
    mwinWin32Platform* platform = window->platform;
    uint32_t capacity = platform->context->limits.textBytesPerWindow;
    LONG size = ImmGetCompositionStringW(context, which, nullptr, 0);
    *lengthOut = 0;
    if (size <= 0 || (uint32_t)size / sizeof(WCHAR) > capacity)
    {
        // Past the buffer: the program's text state cannot follow.
        if (size > 0)
        {
            PostType(window, mwin_eventInputStateReset);
        }
        return 0;
    }
    uint32_t units =
        (uint32_t)ImmGetCompositionStringW(context, which, platform->imeUnits, (DWORD)size) /
        sizeof(WCHAR);
    size_t length = 0;
    muniTextResult converted =
        muniConvertUtf16ToUtf8((const uint16_t*)platform->imeUnits, units, muni_convertReplace,
                               platform->imeBytes, capacity, &length);
    if (converted.status != muni_success)
    {
        PostType(window, mwin_eventInputStateReset);
        return 0;
    }
    *lengthOut = (uint32_t)length;
    return units;
}

static mwinPreeditStyle StyleOf(BYTE attribute)
{
    switch (attribute)
    {
    case ATTR_TARGET_CONVERTED:
    case ATTR_TARGET_NOTCONVERTED:
        return mwin_preeditTarget;
    case ATTR_CONVERTED:
    case ATTR_FIXEDCONVERTED:
        return mwin_preeditConverted;
    default:
        return mwin_preeditUnderline;
    }
}

// The composition's clauses, in bytes, from the attribute of each unit;
// past the last segment the rest joins it. Returns the count, with the
// target clauses' span in *preedit's selection.
static uint32_t Segment(const WCHAR* text, uint32_t units, const BYTE* attributeOf,
                        uint32_t attributes, mwinPreeditSegment* segments,
                        mwinPreeditEvent* preedit)
{
    uint32_t count = 0;
    bool targeted = false;
    for (uint32_t i = 0; i < units; i++)
    {
        mwinPreeditStyle style = i < attributes ? StyleOf(attributeOf[i]) : mwin_preeditPlain;
        uint32_t start = mwinWin32Utf8Before(text, units, i);
        uint32_t end = mwinWin32Utf8Before(text, units, i + 1);
        if (count == 0 || (segments[count - 1].style != style && count < MWIN_MAX_PREEDIT_SEGMENTS))
        {
            segments[count++] = (mwinPreeditSegment){start, 0, style};
        }
        segments[count - 1].length = end - segments[count - 1].start;
        if (style == mwin_preeditTarget)
        {
            preedit->selectionStart = targeted ? preedit->selectionStart : start;
            preedit->selectionEnd = end;
            targeted = true;
        }
    }
    return count;
}

void mwinWin32ShapePreedit(const WCHAR* units, uint32_t count, const BYTE* attributes,
                           LONG attributeCount, LONG caret, mwinPreeditSegment* segments,
                           mwinPreeditEvent* preedit)
{
    preedit->caret = caret >= 0 ? (int32_t)mwinWin32Utf8Before(units, count, (uint32_t)caret) : -1;
    preedit->selectionStart = preedit->selectionEnd =
        preedit->caret >= 0 ? (uint32_t)preedit->caret : 0;
    preedit->segmentCount =
        Segment(units, count, attributes, attributeCount > 0 ? (uint32_t)attributeCount : 0,
                segments, preedit);
    preedit->segments = segments;
}

// The composition as it is now; an empty one ends it.
static void PostComposition(mwinWin32Window* window, HIMC context)
{
    mwinWin32Platform* platform = window->platform;
    uint32_t length = 0;
    uint32_t units = ReadString(window, context, GCS_COMPSTR, &length);
    if (units == 0)
    {
        EndComposition(window);
        return;
    }
    LONG attributes = ImmGetCompositionStringW(context, GCS_COMPATTR, platform->imeAttributes,
                                               platform->context->limits.textBytesPerWindow);
    LONG caret = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinEvent event = {.type = mwin_eventImePreedit, .timeNs = mwinWin32Now()};
    mwinPreeditEvent* preedit = &event.data.preedit;
    preedit->text = platform->imeBytes;
    preedit->length = length;
    mwinWin32ShapePreedit(platform->imeUnits, units, platform->imeAttributes, attributes, caret,
                          segments, preedit);
    Post(window, &event);
}

static void OnComposition(mwinWin32Window* window, LPARAM changes)
{
    HIMC context = ImmGetContext(window->hwnd);
    if (context == nullptr)
    {
        return;
    }
    uint32_t length = 0;
    if ((changes & GCS_RESULTSTR) != 0 && ReadString(window, context, GCS_RESULTSTR, &length) != 0)
    {
        mwinEvent event = {.type = mwin_eventTextInput, .timeNs = mwinWin32Now()};
        event.data.text = (mwinTextEvent){window->platform->imeBytes, length};
        Post(window, &event);
    }
    if ((changes & GCS_COMPSTR) != 0)
    {
        PostComposition(window, context);
    }
    else if ((changes & GCS_RESULTSTR) != 0)
    {
        EndComposition(window);
    }
    ImmReleaseContext(window->hwnd, context);
}

// Places the input method's windows by the caret.
static void Place(const mwinWin32Window* window)
{
    HIMC context = ImmGetContext(window->hwnd);
    if (context == nullptr)
    {
        return;
    }
    float scale = mwinWin32Scale(window->dpi);
    mwinRect caret = window->caret;
    LONG left = lroundf(caret.x * scale);
    LONG top = lroundf(caret.y * scale);
    RECT area = {left, top, lroundf((caret.x + caret.width) * scale),
                 lroundf((caret.y + caret.height) * scale)};
    COMPOSITIONFORM composition = {CFS_POINT, {left, top}, area};
    CANDIDATEFORM candidate = {0, CFS_EXCLUDE, {left, area.bottom}, area};
    (void)ImmSetCompositionWindow(context, &composition);
    (void)ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(window->hwnd, context);
}

bool mwinWin32HandleIme(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                        LRESULT* result)
{
    *result = 0;
    switch (message)
    {
    case WM_IME_SETCONTEXT:
        // The candidate window stays Windows'; the composition is the
        // program's to show.
        *result = DefWindowProcW(window->hwnd, message, wParam,
                                 lParam & ~(LPARAM)ISC_SHOWUICOMPOSITIONWINDOW);
        return true;
    case WM_IME_STARTCOMPOSITION:
        Place(window);
        return true;
    case WM_IME_COMPOSITION:
        OnComposition(window, lParam);
        return true;
    case WM_IME_ENDCOMPOSITION:
        EndComposition(window);
        return true;
    case WM_IME_CHAR:
        // The result came from the composition already.
        return true;
    default:
        return false;
    }
}

mwinOutcome mwinWin32SetTextInput(mwinWin32Window* window, bool enabled, mwinRect caret)
{
    window->caret = caret;
    if (enabled)
    {
        (void)ImmAssociateContextEx(window->hwnd, nullptr, IACE_DEFAULT);
        Place(window);
        return mwin_outcomeDone;
    }
    HIMC context = ImmGetContext(window->hwnd);
    if (context != nullptr)
    {
        (void)ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(window->hwnd, context);
    }
    (void)ImmAssociateContextEx(window->hwnd, nullptr, 0);
    EndComposition(window);
    return mwin_outcomeDone;
}

void mwinWin32StartIme(mwinWin32Window* window)
{
    (void)ImmAssociateContextEx(window->hwnd, nullptr, 0);
}
