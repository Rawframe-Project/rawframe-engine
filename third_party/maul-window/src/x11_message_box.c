// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box drawn on an X server by the library itself, through
// the libxcb it opens at run time (x11_api.h): a window of its own, the
// message in the core "fixed" font, and its buttons outlined. Keys are
// read by the evdev keycodes every X server on Linux uses (Return 36,
// keypad Enter 104, Escape 9); a box shown before any keymap is read
// needs no more.

#include "x11_message_box.h"

#include "allocator.h"
#include "x11_api.h"

#include <stdlib.h>
#include <string.h>

enum
{
    kColumns = 60,
    kLines = 24,
    kPadding = 16,
    kGap = 10,
    kKeyReturn = 36,
    kKeyEnter = 104,
    kKeyEscape = 9,
};

// The message as lines of Latin-1 bytes the core font draws: wrapped at
// a space before kColumns, at kColumns where a word is longer, and at
// each newline; a character past Latin-1 as '?'; lines past kLines left
// out.
typedef struct Lines
{
    char text[kLines][kColumns];
    uint8_t length[kLines];
    int count;
} Lines;

// The next character of the UTF-8 message from `*at`, which it moves
// past.
static uint32_t NextCode(const char* message, size_t length, size_t* at)
{
    const uint8_t first = (uint8_t)message[*at];
    const size_t bytes = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
    uint32_t code = bytes == 1 ? first : first & (0xFFu >> (bytes + 1));
    for (size_t more = 1; more < bytes && *at + more < length; more++)
    {
        code = (code << 6) | ((uint8_t)message[*at + more] & 0x3Fu);
    }
    *at += bytes;
    return code;
}

// A character as the core font's Latin-1 byte.
static char Latin1(uint32_t code)
{
    if (code == '\t')
    {
        return ' ';
    }
    return code < 0x20 || code > 0xFF ? '?' : (char)code;
}

// The line being filled, and its last space.
typedef struct Filling
{
    char text[kColumns];
    int used;
    int lastSpace;
} Filling;

// Ends the line being filled: whole, or, `atSpace`, at its last space,
// the word after it carried on.
static void EndLine(Filling* filling, Lines* lines, bool atSpace)
{
    int cut = filling->used;
    int next = filling->used;
    if (atSpace && filling->lastSpace > 0)
    {
        cut = filling->lastSpace;
        next = cut + 1;
    }
    memcpy(lines->text[lines->count], filling->text, (size_t)cut);
    lines->length[lines->count] = (uint8_t)cut;
    lines->count++;
    memmove(filling->text, filling->text + next, (size_t)(filling->used - next));
    filling->used -= next;
    filling->lastSpace = -1;
}

static void Wrap(const char* message, size_t length, Lines* lines)
{
    memset(lines, 0, sizeof(*lines));
    Filling filling = {.lastSpace = -1};
    size_t at = 0;
    while (at < length && lines->count < kLines)
    {
        const uint32_t code = NextCode(message, length, &at);
        if (code == '\n')
        {
            EndLine(&filling, lines, false);
            continue;
        }
        if (filling.used == kColumns)
        {
            EndLine(&filling, lines, true);
        }
        if (code == ' ')
        {
            filling.lastSpace = filling.used;
        }
        filling.text[filling.used++] = Latin1(code);
    }
    if (filling.used > 0 && lines->count < kLines)
    {
        EndLine(&filling, lines, false);
    }
}

typedef struct Box
{
    const mwinX11Api* api;
    xcb_connection_t* connection;
    xcb_window_t window;
    xcb_gcontext_t gc;
    int cell;
    int ascent;
    int lineHeight;
    Lines lines;
    const char* labels[2];
    int buttons;
    xcb_rectangle_t buttonAt[2];
} Box;

static void Draw(const Box* box)
{
    for (int line = 0; line < box->lines.count; line++)
    {
        box->api->imageText8(box->connection, box->lines.length[line], box->window, box->gc,
                             kPadding, (int16_t)(kPadding + box->ascent + line * box->lineHeight),
                             box->lines.text[line]);
    }
    for (int button = 0; button < box->buttons; button++)
    {
        const xcb_rectangle_t at = box->buttonAt[button];
        box->api->polyRectangle(box->connection, box->window, box->gc, 1, &at);
        const int length = (int)strlen(box->labels[button]);
        box->api->imageText8(box->connection, (uint8_t)length, box->window, box->gc,
                             (int16_t)(at.x + (at.width - length * box->cell) / 2),
                             (int16_t)(at.y + (at.height - box->lineHeight) / 2 + box->ascent),
                             box->labels[button]);
    }
    box->api->flush(box->connection);
}

static xcb_atom_t Atom(const Box* box, const char* name)
{
    xcb_intern_atom_cookie_t cookie =
        box->api->internAtom(box->connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t* reply = box->api->internAtomReply(box->connection, cookie, nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    mwinReleaseSystemMemory(reply);
    return atom;
}

// The button `press` is on: 0 the first, 1 the second, -1 none.
static int Pressed(const Box* box, const xcb_button_press_event_t* press)
{
    for (int button = 0; button < box->buttons && press->detail == 1; button++)
    {
        const xcb_rectangle_t at = box->buttonAt[button];
        if (press->event_x >= at.x && press->event_x <= at.x + at.width && press->event_y >= at.y &&
            press->event_y <= at.y + at.height)
        {
            return button;
        }
    }
    return -1;
}

// What `event` answers: 1 accepts, 0 refuses, -1 nothing. An exposure
// draws the box again; a sent event is answered as a real one is.
static int AnswerOf(const Box* box, const xcb_generic_event_t* event, xcb_atom_t deleteWindow)
{
    switch (event->response_type & 0x7Fu)
    {
    case XCB_EXPOSE:
        if (((const xcb_expose_event_t*)event)->count == 0)
        {
            Draw(box);
        }
        return -1;
    case XCB_KEY_PRESS:
    {
        const xcb_keycode_t key = ((const xcb_key_press_event_t*)event)->detail;
        return key == kKeyReturn || key == kKeyEnter ? 1 : key == kKeyEscape ? 0 : -1;
    }
    case XCB_BUTTON_PRESS:
    {
        const int button = Pressed(box, (const xcb_button_press_event_t*)event);
        return button < 0 ? -1 : button == 0 ? 1 : 0;
    }
    case XCB_CLIENT_MESSAGE:
    {
        const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)event;
        return message->format == 32 && message->data.data32[0] == deleteWindow ? 0 : -1;
    }
    default:
        return -1;
    }
}

// Waits for an answer: true to accept.
static bool Answer(const Box* box, xcb_atom_t deleteWindow)
{
    for (;;)
    {
        xcb_generic_event_t* event = box->api->waitForEvent(box->connection);
        if (event == nullptr)
        {
            // The server went away.
            return false;
        }
        const int answer = AnswerOf(box, event, deleteWindow);
        mwinReleaseSystemMemory(event);
        if (answer >= 0)
        {
            return answer == 1;
        }
    }
}

// The font's cell, the lines, and where the buttons are: the window's
// size in `widthOut` and `heightOut`.
static void Measure(Box* box, const mwinMessageBoxDef* def, xcb_font_t font, int* widthOut,
                    int* heightOut)
{
    const mwinX11Api* api = box->api;
    xcb_query_font_reply_t* metrics =
        api->queryFontReply(box->connection, api->queryFont(box->connection, font), nullptr);
    box->cell = metrics != nullptr && metrics->max_bounds.character_width > 0
                    ? metrics->max_bounds.character_width
                    : 6;
    box->ascent = metrics != nullptr ? metrics->font_ascent : 11;
    box->lineHeight = box->ascent + (metrics != nullptr ? metrics->font_descent : 2) + 2;
    mwinReleaseSystemMemory(metrics);
    Wrap(def->message, def->messageLength, &box->lines);
    static const char* const accepts[] = {"OK", "OK", "Yes"};
    static const char* const refuses[] = {"", "Cancel", "No"};
    box->labels[0] = accepts[def->buttons];
    box->labels[1] = refuses[def->buttons];
    box->buttons = def->buttons == mwin_buttonsOk ? 1 : 2;
    int widest = 0;
    for (int line = 0; line < box->lines.count; line++)
    {
        widest = box->lines.length[line] > widest ? box->lines.length[line] : widest;
    }
    const int buttonWidth = 6 * box->cell + 2 * kPadding;
    const int buttonHeight = box->lineHeight + kGap;
    int width = widest * box->cell + 2 * kPadding;
    const int buttonsWidth = box->buttons * buttonWidth + (box->buttons - 1) * kGap + 2 * kPadding;
    width = width > buttonsWidth ? width : buttonsWidth;
    width = width > 320 ? width : 320;
    const int height = 2 * kPadding + box->lines.count * box->lineHeight + kPadding + buttonHeight;
    for (int button = 0; button < box->buttons; button++)
    {
        box->buttonAt[button] = (xcb_rectangle_t){
            .x = (int16_t)(width - kPadding - (box->buttons - button) * buttonWidth -
                           (box->buttons - 1 - button) * kGap),
            .y = (int16_t)(height - kPadding - buttonHeight),
            .width = (uint16_t)buttonWidth,
            .height = (uint16_t)buttonHeight,
        };
    }
    *widthOut = width;
    *heightOut = height;
}

static mwinResult Show(Box* box, const mwinMessageBoxDef* def, bool* acceptedOut)
{
    const mwinX11Api* api = box->api;
    xcb_screen_t* screen = api->setupRootsIterator(api->getSetup(box->connection)).data;
    const xcb_font_t font = api->generateId(box->connection);
    xcb_void_cookie_t opened = api->openFontChecked(box->connection, font, 5, "fixed");
    xcb_generic_error_t* error = api->requestCheck(box->connection, opened);
    if (error != nullptr)
    {
        mwinReleaseSystemMemory(error);
        return mwin_errorUnsupported;
    }
    int width = 0;
    int height = 0;
    Measure(box, def, font, &width, &height);
    box->window = api->generateId(box->connection);
    const uint32_t values[] = {
        screen->white_pixel,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS,
    };
    const int16_t x =
        (int16_t)(screen->width_in_pixels > width ? (screen->width_in_pixels - width) / 2 : 0);
    const int16_t y =
        (int16_t)(screen->height_in_pixels > height ? (screen->height_in_pixels - height) / 2 : 0);
    error = api->requestCheck(
        box->connection, api->createWindowChecked(
                             box->connection, XCB_COPY_FROM_PARENT, box->window, screen->root, x, y,
                             (uint16_t)width, (uint16_t)height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                             screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values));
    if (error != nullptr)
    {
        mwinReleaseSystemMemory(error);
        api->closeFont(box->connection, font);
        return mwin_errorPlatform;
    }
    // Its title, a dialog of a size that does not change, closed by the
    // window manager as a refusal.
    api->changeProperty(box->connection, XCB_PROP_MODE_REPLACE, box->window,
                        Atom(box, "_NET_WM_NAME"), Atom(box, "UTF8_STRING"), 8,
                        (uint32_t)def->titleLength, def->title);
    api->changeProperty(box->connection, XCB_PROP_MODE_REPLACE, box->window, XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING, 8, (uint32_t)def->titleLength, def->title);
    const xcb_atom_t dialog = Atom(box, "_NET_WM_WINDOW_TYPE_DIALOG");
    api->changeProperty(box->connection, XCB_PROP_MODE_REPLACE, box->window,
                        Atom(box, "_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &dialog);
    // WM_NORMAL_HINTS: its place, and its least and most size the same.
    const uint32_t hints[18] = {[0] = 1u | 4u | 16u | 32u, [1] = (uint32_t)x,
                                [2] = (uint32_t)y,         [5] = (uint32_t)width,
                                [6] = (uint32_t)height,    [7] = (uint32_t)width,
                                [8] = (uint32_t)height};
    api->changeProperty(box->connection, XCB_PROP_MODE_REPLACE, box->window,
                        XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
    const xcb_atom_t deleteWindow = Atom(box, "WM_DELETE_WINDOW");
    api->changeProperty(box->connection, XCB_PROP_MODE_REPLACE, box->window,
                        Atom(box, "WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1, &deleteWindow);

    box->gc = api->generateId(box->connection);
    const uint32_t gcValues[] = {screen->black_pixel, screen->white_pixel, font};
    api->createGc(box->connection, box->gc, box->window,
                  XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT, gcValues);
    api->mapWindow(box->connection, box->window);
    api->flush(box->connection);
    *acceptedOut = Answer(box, deleteWindow);
    api->freeGc(box->connection, box->gc);
    api->destroyWindow(box->connection, box->window);
    api->closeFont(box->connection, font);
    api->flush(box->connection);
    return mwin_success;
}

mwinResult mwinX11MessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    const char* display = getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0')
    {
        return mwin_errorUnsupported;
    }
    mwinX11Api api;
    if (mwinLoadX11(&api) != mwin_success)
    {
        return mwin_errorUnsupported;
    }
    mwinResult result = mwin_errorUnsupported;
    xcb_connection_t* connection = api.connect(nullptr, nullptr);
    if (connection != nullptr && api.connectionHasError(connection) == 0)
    {
        Box box = {.api = &api, .connection = connection};
        result = Show(&box, def, acceptedOut);
    }
    if (connection != nullptr)
    {
        api.disconnect(connection);
    }
    mwinUnloadX11(&api);
    return result;
}
