// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods over the session bus, for the X11 backend (mwin-0030): Fcitx
// 5 (org.freedesktop.portal.Fcitx) when XMODIFIERS names fcitx, else
// IBus through its portal (org.freedesktop.portal.IBus), else Fcitx 5.
// One input context, made the first time a window takes text, follows
// the window with the focus that takes text, its caret in root
// coordinates. While it has one, each key goes to the input method
// without waiting and is held, with every key after it, in order until
// its answer: a key the method took is dropped, and the release of a
// press it took; one it did not take, or whose answer took more than
// 100 ms, is posted as the backend made it.
// Committed text and compositions come as signals and are posted to the
// window at once.

#ifndef MAUL_WINDOW_SRC_LINUX_IME_H
#define MAUL_WINDOW_SRC_LINUX_IME_H

#include "core.h"
#include "linux_bus.h"

// The keys held at once, the longest text one key types, and the bytes
// of an input context's path.
#define MWIN_IME_HELD       32
#define MWIN_IME_TEXT_BYTES 64
#define MWIN_IME_PATH_BYTES 128

// A key held for the input method's answer: the call (none for a key
// held only behind others), the window, its X keycode, and the records
// the backend made for it.
typedef struct mwinImeKey
{
    mwinBusCall call;
    uint32_t slot;
    uint8_t keycode;
    mwinEvent key;
    char text[MWIN_IME_TEXT_BYTES];
    uint32_t length;
} mwinImeKey;

typedef struct mwinLinuxIme
{
    mwinContext* context;
    mwinLinuxBus* bus;
    // The framework asked (0 none yet, then Fcitx or IBus), which ones
    // were tried, and whether the input context is made.
    uint8_t framework;
    uint8_t tried;
    bool ready;
    bool listening;
    mwinBusCall create;
    char path[MWIN_IME_PATH_BYTES];
    // The window the context follows, or -1, and its caret.
    int32_t focus;
    mwinRect caret;
    // The keys held, a ring from first, and by X keycode those whose
    // press the method took, whose release goes too.
    mwinImeKey held[MWIN_IME_HELD];
    uint32_t first;
    uint32_t count;
    uint8_t taken[32];
} mwinLinuxIme;

void mwinImeStart(mwinLinuxIme* ime, mwinContext* context, mwinLinuxBus* bus);

// The window with the focus that takes text, or -1, and its caret in
// root coordinates: the context is made the first time there is one.
void mwinImeFocus(mwinLinuxIme* ime, int32_t slot, mwinRect caret);

// Offers a key: true when it is held, and posted or dropped later;
// false when the backend posts it now. The keysym, the X keycode and
// the X modifier state go to the input method.
bool mwinImeOffer(mwinLinuxIme* ime, uint32_t slot, uint32_t keysym, uint32_t keycode,
                  uint32_t state, const mwinEvent* key, const char* text, uint32_t length,
                  uint64_t nowNs);

// Posts or drops the keys whose answers came; after the bus's pump.
void mwinImePump(mwinLinuxIme* ime, uint64_t nowNs);

// Lets the context go, before the bus closes; the keys held are posted.
void mwinImeStop(mwinLinuxIme* ime);

#endif // MAUL_WINDOW_SRC_LINUX_IME_H
