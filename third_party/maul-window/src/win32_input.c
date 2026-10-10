// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 keyboard, mouse and cursor input.

#include "win32_input.h"

#include "cursor.h"
#include "win32_icon.h"

#include "maul-unicode/encoding.h"

#include <math.h>
#include <string.h>

// The key code of each scan code, from 0x00 to 0x7F, then of each
// extended one (an E0 prefix) from 0x80; 0 where the key has none. The
// ISO key beside Enter has the scan code of Backslash, so
// mwin_codeIntlHash never comes from here.
// clang-format off
static const uint8_t s_codes[256] = {
      0,  41,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  45,  46,  42,  43,
     20,  26,   8,  21,  23,  28,  24,  12,  18,  19,  47,  48,  40, 224,   4,  22,
      7,   9,  10,  11,  13,  14,  15,  51,  52,  53, 225,  49,  29,  27,   6,  25,
      5,  17,  16,  54,  55,  56, 229,  85, 226,  44,  57,  58,  59,  60,  61,  62,
     63,  64,  65,  66,  67,  72,  71,  95,  96,  97,  86,  92,  93,  94,  87,  89,
     90,  91,  98,  99,   0,   0, 100,  68,  69, 103,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,   0,
    136, 145, 144, 135,   0,   0, 115,   0,   0, 138,   0, 139,   0, 137, 133,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  88, 228,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,  84,   0,  70, 230,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,  83,  72,  74,  82,  75,   0,  80,   0,  79,   0,  77,
     81,  78,  73,  76,   0,   0,   0,   0,   0,   0,   0, 227, 231, 101,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};
// clang-format on

// The system's cursors of each shape.
static const LPCWSTR s_cursors[] = {
    IDC_ARROW,  IDC_IBEAM,    IDC_HAND,     IDC_CROSS, IDC_SIZEALL, IDC_SIZEWE,
    IDC_SIZENS, IDC_SIZENESW, IDC_SIZENWSE, IDC_NO,    IDC_WAIT,    IDC_APPSTARTING,
};

// The scan code of a key message, with 0x80 for an extended key; one
// that came without a scan code (a synthesized key) is found from its
// virtual key.
static uint32_t ScanCodeOf(WPARAM key, LPARAM flags)
{
    uint32_t scan = (uint32_t)(flags >> 16) & 0xFFu;
    bool extended = ((flags >> 24) & 1) != 0;
    if (scan == 0)
    {
        UINT mapped = MapVirtualKeyW((UINT)key, MAPVK_VK_TO_VSC_EX);
        scan = mapped & 0xFFu;
        extended = (mapped & 0xFF00u) == 0xE000u;
    }
    return (scan & 0x7Fu) | (extended ? 0x80u : 0u);
}

// What a key types with no modifier in the current layout: a dead key
// counts its accent.
static mwinKey MeaningOf(uint32_t scan, mwinKeyCode code)
{
    HKL layout = GetKeyboardLayout(0);
    UINT native = (scan & 0x7Fu) | ((scan & 0x80u) != 0 ? 0xE000u : 0u);
    UINT key = MapVirtualKeyExW(native, MAPVK_VSC_TO_VK_EX, layout);
    BYTE state[256] = {0};
    WCHAR text[4];
    // Flag 4 leaves the keyboard state, and a dead key waiting, as they
    // are.
    int count = key != 0 ? ToUnicodeEx(key, native, state, text, 4, 4, layout) : 0;
    if (count != 0 && text[0] >= 0x20 && text[0] != 0x7F && (text[0] < 0xD800 || text[0] > 0xDFFF))
    {
        return text[0];
    }
    return MWIN_KEY_NAMED | code;
}

static mwinModifiers Modifiers(void)
{
    mwinModifiers modifiers = 0;
    modifiers |= (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? mwin_modShift : 0;
    modifiers |= (GetKeyState(VK_CONTROL) & 0x8000) != 0 ? mwin_modControl : 0;
    modifiers |= (GetKeyState(VK_MENU) & 0x8000) != 0 ? mwin_modAlt : 0;
    modifiers |= ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0 ? mwin_modMeta : 0;
    modifiers |= (GetKeyState(VK_CAPITAL) & 1) != 0 ? mwin_modCapsLock : 0;
    modifiers |= (GetKeyState(VK_NUMLOCK) & 1) != 0 ? mwin_modNumLock : 0;
    return modifiers;
}

static void Post(mwinWin32Window* window, const mwinEvent* event)
{
    mwinPost(window->platform->context, window->slot, event);
}

static void PostKey(mwinWin32Window* window, mwinEventType type, uint32_t scan, bool repeat)
{
    mwinKeyCode code = s_codes[scan];
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinWin32Now();
    event.data.key = (mwinKeyEvent){code, Modifiers(), MeaningOf(scan, code), repeat};
    Post(window, &event);
}

static void OnKey(mwinWin32Window* window, UINT message, WPARAM key, LPARAM flags)
{
    uint32_t scan = ScanCodeOf(key, flags);
    // An input method's key and typed text (VK_PACKET) are no key.
    if (s_codes[scan] == mwin_codeUnknown || key == VK_PROCESSKEY || key == VK_PACKET)
    {
        return;
    }
    bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    // Windows sends Print Screen's release alone.
    if (!down && s_codes[scan] == mwin_codePrintScreen)
    {
        PostKey(window, mwin_eventKeyDown, scan, false);
    }
    bool repeat = down && ((flags >> 30) & 1) != 0;
    PostKey(window, down ? mwin_eventKeyDown : mwin_eventKeyUp, scan, repeat);
}

// A UTF-16 unit of typed text; a character outside the BMP comes in
// two.
static void OnCharacter(mwinWin32Window* window, WPARAM unit)
{
    uint32_t character = (uint32_t)unit;
    if (character >= 0xD800 && character <= 0xDBFF)
    {
        window->highSurrogate = (WCHAR)character;
        return;
    }
    if (character >= 0xDC00 && character <= 0xDFFF)
    {
        character = window->highSurrogate != 0
                        ? 0x10000u + (((uint32_t)window->highSurrogate - 0xD800u) << 10) +
                              (character - 0xDC00u)
                        : 0;
    }
    window->highSurrogate = 0;
    // Control characters are keys, not text.
    char text[4];
    size_t length = 0;
    if (character < 0x20 || character == 0x7F ||
        muniEncodeUtf8(character, text, &length) != muni_success)
    {
        return;
    }
    mwinEvent event = {0};
    event.type = mwin_eventTextInput;
    event.timeNs = mwinWin32Now();
    event.data.text = (mwinTextEvent){text, (uint32_t)length};
    Post(window, &event);
}

static mwinPosition PositionOf(const mwinWin32Window* window, LPARAM place)
{
    float scale = mwinWin32Scale(window->dpi);
    return (mwinPosition){(float)(int16_t)LOWORD(place) / scale,
                          (float)(int16_t)HIWORD(place) / scale};
}

static void PostPointer(mwinWin32Window* window, mwinEventType type, mwinPosition position,
                        mwinMouseButton button)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinWin32Now();
    event.data.pointer =
        (mwinPointerEvent){position, Modifiers(), window->buttons, button, window->clicks.clicks};
    Post(window, &event);
}

// Windows tracks the pointer's leaving of the client area and of the
// frame apart; over custom chrome, hit regions of the client area are
// frame to Windows, and both are the window's to the program.
static void OnMouseMove(mwinWin32Window* window, LPARAM place, bool frame)
{
    mwinPosition position = PositionOf(window, place);
    if (!window->tracking || window->trackingFrame != frame)
    {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | (frame ? TME_NONCLIENT : 0u),
                                 window->hwnd, 0};
        window->tracking = TrackMouseEvent(&track) != 0;
        window->trackingFrame = frame;
    }
    if (!window->pointerInside)
    {
        window->pointerInside = true;
        PostPointer(window, mwin_eventCursorEntered, position, 0);
    }
    PostPointer(window, mwin_eventCursorMoved, position, 0);
}

// A point of the desktop in the client area's pixels, as a message
// carries it, or false outside the client area.
static bool ClientPlace(const mwinWin32Window* window, POINT point, LPARAM* place)
{
    RECT client;
    GetClientRect(window->hwnd, &client);
    ScreenToClient(window->hwnd, &point);
    *place = MAKELPARAM((WORD)(int16_t)point.x, (WORD)(int16_t)point.y);
    return PtInRect(&client, point) != 0;
}

static POINT PointOf(LPARAM place)
{
    return (POINT){(int16_t)LOWORD(place), (int16_t)HIWORD(place)};
}

// The pointer left what Windows tracked: from the client area into a
// hit region or back, still over the client area, it did not leave.
static void OnLeave(mwinWin32Window* window)
{
    window->tracking = false;
    POINT cursor;
    LPARAM place = 0;
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == window->hwnd &&
        ClientPlace(window, cursor, &place))
    {
        return;
    }
    if (window->pointerInside)
    {
        window->pointerInside = false;
        PostPointer(window, mwin_eventCursorLeft, (mwinPosition){0}, 0);
    }
}

static mwinMouseButton ButtonOf(UINT message, WPARAM wParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return mwin_buttonLeft;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return mwin_buttonRight;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return mwin_buttonMiddle;
    default:
        return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? mwin_buttonBack : mwin_buttonForward;
    }
}

// A button: the window keeps the mouse while any is held.
static void OnButton(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM place)
{
    mwinMouseButton button = ButtonOf(message, wParam);
    bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
    mwinPosition position = PositionOf(window, place);
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (down)
    {
        if (window->buttons == 0)
        {
            SetCapture(window->hwnd);
        }
        window->buttons |= bit;
        float scale = mwinWin32Scale(window->dpi);
        (void)mwinCountClickWithin(&window->clicks, button, position, mwinWin32Now(),
                                   (uint64_t)GetDoubleClickTime() * 1000000u,
                                   (float)GetSystemMetrics(SM_CXDOUBLECLK) / 2.0f / scale);
    }
    else
    {
        window->buttons &= (uint8_t)~bit;
        if (window->buttons == 0)
        {
            ReleaseCapture();
        }
    }
    PostPointer(window, down ? mwin_eventButtonDown : mwin_eventButtonUp, position, button);
}

// A press or release over a maximize button in the client area, which
// Windows sends as the frame's: the program's, as any of the client
// area. Windows' own handling would maximize the window.
static bool OnFrameButton(mwinWin32Window* window, UINT message, WPARAM hit, LPARAM screen)
{
    LPARAM place = 0;
    if (hit != HTMAXBUTTON || !ClientPlace(window, PointOf(screen), &place))
    {
        return false;
    }
    UINT client = message - WM_NCLBUTTONDOWN + WM_LBUTTONDOWN;
    // The frame's double clicks are presses: the program counts clicks.
    client -= (client - WM_LBUTTONDOWN) % 3 == 2 ? 2 : 0;
    OnButton(window, client, 0, place);
    return true;
}

static void OnWheel(mwinWin32Window* window, UINT message, WPARAM wParam)
{
    float detents = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
    mwinEvent event = {0};
    event.type = mwin_eventWheel;
    event.timeNs = mwinWin32Now();
    // Windows counts away from the user and to the right as positive.
    event.data.wheel = message == WM_MOUSEWHEEL ? (mwinWheelEvent){0.0f, detents}
                                                : (mwinWheelEvent){detents, 0.0f};
    Post(window, &event);
}

static bool IsHidden(mwinCursorMode mode)
{
    return mode == mwin_cursorHidden || mode == mwin_cursorConfinedHidden ||
           mode == mwin_cursorCaptured;
}

// Raw mouse motion, before Windows' acceleration, while captured.
static void OnRawInput(mwinWin32Window* window, LPARAM handle)
{
    RAWINPUT input;
    UINT size = sizeof(input);
    if (window->cursorMode != mwin_cursorCaptured ||
        GetRawInputData((HRAWINPUT)mwinWin32Pointer(handle), RID_INPUT, &input, &size,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1 ||
        input.header.dwType != RIM_TYPEMOUSE ||
        (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
    {
        return;
    }
    mwinEvent event = {0};
    event.type = mwin_eventRawPointerDelta;
    event.timeNs = mwinWin32Now();
    event.data.delta =
        (mwinDeltaEvent){(float)input.data.mouse.lLastX, (float)input.data.mouse.lLastY};
    Post(window, &event);
}

// The keyboard's messages.
static bool HandleKeyboard(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                           LRESULT* result)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        OnKey(window, message, wParam, lParam);
        // F10 would enter menu mode; the other system keys stay Windows'.
        return (message != WM_SYSKEYDOWN && message != WM_SYSKEYUP) || wParam == VK_F10;
    case WM_CHAR:
        OnCharacter(window, wParam);
        return true;
    case WM_SYSCHAR:
        // Alt with a key would beep for a menu the window lacks.
        return wParam != ' ';
    case WM_SYSCOMMAND:
        // Alt alone would enter menu mode; Alt+Space opens the menu.
        return (wParam & 0xFFF0) == SC_KEYMENU && lParam == 0;
    case WM_INPUTLANGCHANGE:
    {
        mwinEvent event = {.type = mwin_eventKeyboardLayoutChanged, .timeNs = mwinWin32Now()};
        mwinPostGlobal(window->platform->context, &event);
        *result = TRUE;
        return true;
    }
    default:
        return false;
    }
}

bool mwinWin32HandleInput(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                          LRESULT* result)
{
    *result = 0;
    switch (message)
    {
    case WM_MOUSEMOVE:
        OnMouseMove(window, lParam, false);
        return true;
    case WM_NCMOUSEMOVE:
    {
        LPARAM place = 0;
        if (ClientPlace(window, PointOf(lParam), &place))
        {
            OnMouseMove(window, place, true);
        }
        // Windows goes on with the frame's own, as snap layouts need.
        return false;
    }
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
        OnLeave(window);
        return message == WM_MOUSELEAVE;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
        return OnFrameButton(window, message, wParam, lParam);
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        OnButton(window, message, wParam, lParam);
        *result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
        return true;
    case WM_CAPTURECHANGED:
        // Windows took the mouse, for a menu or a drag of the frame.
        window->buttons = 0;
        return false;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        OnWheel(window, message, wParam);
        return true;
    case WM_INPUT:
        OnRawInput(window, lParam);
        return false;
    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) != HTCLIENT)
        {
            return false;
        }
        HCURSOR image = mwinWin32CursorOf(window);
        SetCursor(IsHidden(window->cursorMode) ? nullptr
                  : image != nullptr ? image
                                     : LoadCursorW(nullptr, s_cursors[window->cursorShape]));
        *result = TRUE;
        return true;
    }
    default:
        return HandleKeyboard(window, message, wParam, lParam, result);
    }
}

void mwinWin32ClipCursor(const mwinWin32Window* window, bool focused)
{
    mwinCursorMode mode = window->cursorMode;
    bool clipped = mode == mwin_cursorConfined || mode == mwin_cursorConfinedHidden ||
                   mode == mwin_cursorCaptured;
    if (!focused || !clipped)
    {
        ClipCursor(nullptr);
        return;
    }
    RECT client;
    GetClientRect(window->hwnd, &client);
    MapWindowPoints(window->hwnd, nullptr, (POINT*)&client, 2);
    if (mode == mwin_cursorCaptured)
    {
        // A point in the middle keeps the pointer off every edge.
        LONG x = (client.left + client.right) / 2;
        LONG y = (client.top + client.bottom) / 2;
        client = (RECT){x, y, x + 1, y + 1};
    }
    ClipCursor(&client);
}

mwinOutcome mwinWin32SetCursorMode(mwinWin32Window* window, mwinCursorMode mode)
{
    mwinWin32Platform* platform = window->platform;
    if (mode == mwin_cursorCaptured && !platform->rawInput)
    {
        // The mouse's raw input goes to the window with focus.
        RAWINPUTDEVICE device = {0x01, 0x02, 0, nullptr};
        platform->rawInput = RegisterRawInputDevices(&device, 1, sizeof(device)) != 0;
        if (!platform->rawInput)
        {
            return mwin_outcomeFailed;
        }
    }
    window->cursorMode = mode;
    mwinWin32ClipCursor(window, GetFocus() == window->hwnd);
    // The cursor changes at once when it is over the window.
    POINT point;
    if (GetCursorPos(&point) && WindowFromPoint(point) == window->hwnd)
    {
        SendMessageW(window->hwnd, WM_SETCURSOR, (WPARAM)window->hwnd, HTCLIENT);
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinWin32SetCursorShape(mwinWin32Window* window, mwinCursorShape shape)
{
    window->cursorShape = shape;
    window->cursorImage = (mwinCursorId){0};
    POINT point;
    if (GetCursorPos(&point) && WindowFromPoint(point) == window->hwnd)
    {
        SendMessageW(window->hwnd, WM_SETCURSOR, (WPARAM)window->hwnd, HTCLIENT);
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinWin32SetCursorImage(mwinWin32Window* window, mwinCursorId cursor)
{
    window->cursorImage = cursor;
    POINT point;
    if (GetCursorPos(&point) && WindowFromPoint(point) == window->hwnd)
    {
        SendMessageW(window->hwnd, WM_SETCURSOR, (WPARAM)window->hwnd, HTCLIENT);
    }
    return mwinWin32CursorOf(window) != nullptr ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinKey mwinWin32MapKeyCode(mwinKeyCode code)
{
    for (uint32_t scan = 1; scan < 256 && code != mwin_codeUnknown; scan++)
    {
        if (s_codes[scan] == code)
        {
            return MeaningOf(scan, code);
        }
    }
    return MWIN_KEY_NAMED | code;
}

mwinResult mwinWin32KeyboardLayout(char* buffer, size_t capacity, size_t* lengthOut)
{
    // The layout's language as a BCP 47 tag: en-US, tr-TR.
    WCHAR name[LOCALE_NAME_MAX_LENGTH];
    LCID language = MAKELCID(LOWORD((UINT_PTR)GetKeyboardLayout(0)), SORT_DEFAULT);
    int units = LCIDToLocaleName(language, name, LOCALE_NAME_MAX_LENGTH, 0);
    size_t length = 0;
    if (units > 1)
    {
        muniTextResult result =
            muniConvertUtf16ToUtf8((const uint16_t*)name, (size_t)units - 1, muni_convertReplace,
                                   buffer, capacity, &length);
        if (result.status != muni_success && result.status != muni_errorCapacity)
        {
            length = 0;
        }
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

HCURSOR mwinWin32CursorOf(const mwinWin32Window* window)
{
    mwinCursor* cursor = mwinFindCursor(window->platform->context, window->cursorImage);
    if (cursor == nullptr)
    {
        return nullptr;
    }
    uint32_t image = mwinCursorImageFor(cursor, mwinWin32Scale(window->dpi));
    if (cursor->native[image] == nullptr)
    {
        uint32_t x = 0;
        uint32_t y = 0;
        mwinCursorHotspotOf(cursor, image, &x, &y);
        cursor->native[image] = mwinWin32MakeIcon(&cursor->images->images[image], true, x, y);
    }
    return cursor->native[image];
}

void mwinWin32ReleaseCursor(mwinContext* context, uint32_t slot)
{
    // Its windows show the default shape from here.
    mwinWin32Platform* platform = context->backendData;
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinWin32Window* window = &platform->windows[i];
        if (window->hwnd != nullptr && window->cursorImage.index1 == slot + 1)
        {
            (void)mwinWin32SetCursorShape(window, mwin_shapeDefault);
        }
    }
    mwinCursor* cursor = &context->cursors[slot];
    for (uint32_t i = 0; i < MWIN_CURSOR_IMAGES; i++)
    {
        if (cursor->native[i] != nullptr)
        {
            DestroyCursor(cursor->native[i]);
            cursor->native[i] = nullptr;
        }
    }
}
