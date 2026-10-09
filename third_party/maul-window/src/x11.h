// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the X11 backend keeps: the connection, the screen, the atoms it
// uses, and per window slot and per monitor what the X server has told.
// One block from the context's allocator holds it all.

#ifndef MAUL_WINDOW_SRC_X11_H
#define MAUL_WINDOW_SRC_X11_H

#include "clicks.h"
#include "core.h"
#include "linux_ime.h"
#include "linux_pad.h"
#include "linux_services.h"
#include "monotonic.h"
#include "selection_reads.h"
#include "x11_api.h"
#include "x11_pen.h"
#include "x11_scroll.h"
#include "x11_touch.h"
#include "xkb_keyboard.h"

#include "maul-window/clipboard.h"

// The atoms the backend interns at start, in the order of s_atomNames
// in backend_x11.c.
enum
{
    mwin_atomWmProtocols,
    mwin_atomWmDeleteWindow,
    mwin_atomWmState,
    mwin_atomWmChangeState,
    mwin_atomNetWmPing,
    mwin_atomNetWmName,
    mwin_atomNetWmPid,
    mwin_atomUtf8String,
    mwin_atomNetWmState,
    mwin_atomNetWmStateFullscreen,
    mwin_atomNetWmStateMaximizedVert,
    mwin_atomNetWmStateMaximizedHorz,
    mwin_atomNetWmStateAbove,
    mwin_atomNetWmStateHidden,
    mwin_atomNetWmWindowOpacity,
    mwin_atomNetWmIcon,
    mwin_atomNetActiveWindow,
    mwin_atomNetSupportingWmCheck,
    mwin_atomMotifWmHints,
    mwin_atomClipboard,
    mwin_atomTargets,
    mwin_atomTimestamp,
    mwin_atomIncr,
    mwin_atomTextPlainUtf8,
    mwin_atomSelection,
    mwin_atomXdndAware,
    mwin_atomXdndEnter,
    mwin_atomXdndPosition,
    mwin_atomXdndStatus,
    mwin_atomXdndLeave,
    mwin_atomXdndDrop,
    mwin_atomXdndFinished,
    mwin_atomXdndSelection,
    mwin_atomXdndTypeList,
    mwin_atomXdndActionCopy,
    mwin_atomUriList,
    mwin_atomNetWmWindowType,
    mwin_atomNetWmWindowTypeDialog,
    mwin_atomNetWmWindowTypePopupMenu,
    mwin_atomNetWmWindowTypeTooltip,
    mwin_atomNetWmMoveresize,
    mwin_atomEdid,
    mwin_atomVrrCapable,
    mwin_atomAbsPressure,
    mwin_atomAbsTiltX,
    mwin_atomAbsTiltY,
    mwin_atomAbsMtPressure,
    mwin_atomStylus,
    mwin_atomEraser,
    mwin_atomTablet,
    MWIN_X11_ATOMS,
};

typedef struct mwinX11Platform mwinX11Platform;

// A window, and what the X server told of it last.
typedef struct mwinX11Window
{
    mwinX11Platform* platform;
    uint32_t slot;
    xcb_window_t window;
    // Its place on the desktop and its size, in pixels.
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    // The size bounds and aspect ratio of the normal hints, in pixels.
    uint32_t minimumWidth;
    uint32_t minimumHeight;
    uint32_t maximumWidth;
    uint32_t maximumHeight;
    uint32_t aspectWidth;
    uint32_t aspectHeight;
    // The monitor slot it is on, or -1.
    int32_t monitor;
    // A mode request waits for the window manager's _NET_WM_STATE, or
    // -1, until its deadline.
    int32_t modeRequest;
    uint64_t modeDeadlineNs;
    // The cursor the program asked for over the window: a shape, or a
    // cursor made from images when cursorImage is live (mwin-0027); and
    // whether the pointer is grabbed to keep it inside.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    mwinCursorId cursorImage;
    bool confined;
    // A popup's place from its owner's corner, in pixels.
    int32_t offsetX;
    int32_t offsetY;
    // Whether the program takes text in it, and its caret, in window
    // coordinates (mwinRequestTextInput).
    bool textInput;
    mwinRect caret;
} mwinX11Window;

// The core keyboard through XKB: the device, XKB's event code, the
// keys held, which tell the X server's repeats from new presses, and
// the window with the keyboard's focus, or -1.
typedef struct mwinX11Keyboard
{
    mwinXkbKeyboard xkb;
    int32_t device;
    uint8_t event;
    uint8_t held[32];
    int32_t focus;
} mwinX11Keyboard;

// The core pointer: the window it is over, or -1, where, the buttons
// held and the last press, and the XI2 device of the event read, 0 for
// a core event.
typedef struct mwinX11Pointer
{
    int32_t focus;
    mwinPosition position;
    uint8_t buttons;
    mwinClickCounter clicks;
    uint16_t device;
} mwinX11Pointer;

// The cursors the backend made: an empty one that hides the pointer,
// and each shape loaded from the cursor theme, 0 before it is. Cursors
// from images need RENDER's 32-bit ARGB picture format, looked up the
// first time one is asked for: 0 where there is none.
typedef struct mwinX11Cursors
{
    xcb_cursor_context_t* context;
    xcb_cursor_t blank;
    xcb_cursor_t shapes[16];
    bool formatSought;
    xcb_render_pictformat_t format;
} mwinX11Cursors;

// A monitor from RandR, by the atom of its name.
typedef struct mwinX11Output
{
    xcb_atom_t name;
    int32_t monitor;
    bool seen;
} mwinX11Output;

// The most readers served the program's text or data a piece at a time
// at once.
#define MWIN_X11_SENDS 4

// The selections the program owns and reads: CLIPBOARD and PRIMARY.
enum
{
    mwin_x11Clipboard,
    mwin_x11Primary,
    MWIN_X11_SELECTIONS,
};

// The clipboard and the primary selection: a hidden window that owns
// them and receives them, made at its first use; for each, whether the
// program owns it and since when, and for CLIPBOARD the atoms of its
// data's MIME types; the readers its text or an item goes to in pieces
// (INCR), the selection and the item (-1 the text) each takes; and a
// read under way, what it is of, its bytes in a block of capacity
// bytes from the allocator.
typedef struct mwinX11Clipboard
{
    xcb_window_t window;
    bool owned[MWIN_X11_SELECTIONS];
    xcb_timestamp_t ownedTime[MWIN_X11_SELECTIONS];
    xcb_atom_t types[MWIN_CLIPBOARD_ITEMS];
    struct
    {
        xcb_window_t requestor;
        xcb_atom_t property;
        xcb_atom_t type;
        uint8_t selection;
        int8_t item;
        uint32_t offset;
        uint64_t deadlineNs;
    } sends[MWIN_X11_SENDS];
    // Waiting for the owner's answer, then for its pieces.
    bool reading;
    bool incremental;
    mwinSelectionRead read;
    uint64_t deadlineNs;
    char* buffer;
    uint32_t used;
    uint32_t capacity;
} mwinX11Clipboard;

// A drag another client makes over one of the program's windows
// (XDND): its source and version, the window, what it carries and the
// type its text comes in, where it is and whether it was reported; and
// a drop being converted, one type after the other, until a deadline.
typedef struct mwinX11Drag
{
    xcb_window_t source;
    uint32_t version;
    int32_t slot;
    mwinDragContents contents;
    xcb_atom_t textType;
    mwinPosition position;
    bool entered;
    bool dropping;
    xcb_atom_t converting;
    xcb_timestamp_t time;
    uint64_t deadlineNs;
} mwinX11Drag;

struct mwinX11Platform
{
    mwinX11Api api;
    mwinContext* context;
    // The gamepads, with the gamepad component.
    mwinLinuxPads pads;
    xcb_connection_t* connection;
    xcb_screen_t* screen;
    xcb_atom_t atoms[MWIN_X11_ATOMS];
    // A window manager that follows EWMH runs.
    bool windowManager;
    // Logical units per pixel from Xft.dpi, 1 where it is not set.
    float scale;
    // RandR's first event code, 0 without RandR 1.5, and XInput's major
    // opcode, 0 without XInput 2 raw motion.
    uint8_t randrEvent;
    uint8_t xinputOpcode;
    // XI 2.1: windows read the pointer through XI2, with the scroll
    // valuators of its devices (x11_scroll.h).
    bool smoothScroll;
    mwinX11Scroll scroll;
    // The devices that are pens, whose events make pen records
    // (x11_pen.h).
    mwinX11Pens pens;
    // XI 2.2: windows read touches, those of the touch screens making
    // touch records (x11_touch.h).
    bool touch;
    mwinX11Touches touches;
    // libxkbcommon, and the keyboard, where both load.
    mwinXkbApi xkbApi;
    mwinX11Keyboard keyboard;
    mwinX11Pointer pointer;
    mwinX11Cursors cursors;
    mwinX11Clipboard clipboard;
    mwinX11Drag drag;
    // Addresses, files and the bus (linux_services.c).
    mwinLinuxServices services;
    // The input method over the session bus (linux_ime.c, W40).
    mwinLinuxIme ime;
    // The time of the latest key or button event, which taking the
    // selection quotes.
    xcb_timestamp_t inputTime;
    // The connection failed; the loop stops.
    bool failed;
    // One per window slot, and one per monitor slot.
    mwinX11Window* windows;
    mwinX11Output* outputs;
};

// The slot of the window with an X id, or -1.
static inline int32_t mwinX11SlotOf(const mwinX11Platform* platform, xcb_window_t window)
{
    for (uint32_t i = 0; window != 0 && i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].window == window)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

#endif // MAUL_WINDOW_SRC_X11_H
