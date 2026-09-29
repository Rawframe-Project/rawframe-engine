// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Custom chrome on X11.

#include "x11_chrome.h"

#include "chrome.h"

#include <string.h>

// _NET_WM_MOVERESIZE's directions, by hit kind from the caption.
static const uint32_t s_directions[] = {8, 7, 3, 1, 5, 0, 2, 6, 4};

void mwinX11SendToRoot(const mwinX11Platform* platform, xcb_window_t window, xcb_atom_t type,
                       const uint32_t data[5])
{
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = window;
    message.type = type;
    memcpy(message.data.data32, data, sizeof(message.data.data32));
    platform->api.sendEvent(platform->connection, 0, platform->screen->root,
                            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                            (const char*)&message);
}

bool mwinX11PressChrome(const mwinX11Platform* platform, uint32_t slot,
                        const xcb_button_press_event_t* event, mwinPosition at, uint8_t clicks)
{
    const mwinWindow* core = &platform->context->windows[slot];
    mwinHitKind kind = mwinHitAt(core, at.x, at.y);
    if (!mwinHitMoves(kind))
    {
        return false;
    }
    const xcb_atom_t* atoms = platform->atoms;
    xcb_window_t window = platform->windows[slot].window;
    if (kind == mwin_hitCaption && clicks == 2)
    {
        // Remove (0) or add (1), both directions, from a program (1).
        const uint32_t data[5] = {core->state.mode == mwin_modeMaximized ? 0u : 1u,
                                  atoms[mwin_atomNetWmStateMaximizedVert],
                                  atoms[mwin_atomNetWmStateMaximizedHorz], 1, 0};
        mwinX11SendToRoot(platform, window, atoms[mwin_atomNetWmState], data);
        return true;
    }
    // The window manager grabs the pointer itself, which the X server's
    // grab for the press would refuse; a press read through XI2 grabbed
    // its device.
    platform->api.ungrabPointer(platform->connection, XCB_CURRENT_TIME);
    if (platform->pointer.device != 0)
    {
        platform->api.xiUngrabDevice(platform->connection, XCB_CURRENT_TIME,
                                     platform->pointer.device);
    }
    const uint32_t data[5] = {(uint32_t)event->root_x, (uint32_t)event->root_y,
                              s_directions[kind - mwin_hitCaption], XCB_BUTTON_INDEX_1, 1};
    mwinX11SendToRoot(platform, window, atoms[mwin_atomNetWmMoveresize], data);
    return true;
}
