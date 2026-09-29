// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A keyboard as libxkbcommon sees it, for the Linux backends: the
// keymap and state the window system gives, compose sequences (dead
// keys) from the locale's table, what a key means in the current layout
// and the text it types. The backends post the records.

#ifndef MAUL_WINDOW_SRC_XKB_KEYBOARD_H
#define MAUL_WINDOW_SRC_XKB_KEYBOARD_H

#include "xkb_api.h"

#include "maul-window/input.h"

// The longest text one key types.
#define MWIN_XKB_TEXT_BYTES 64

typedef struct mwinXkbKeyboard
{
    const mwinXkbApi* api;
    struct xkb_context* context;
    struct xkb_keymap* keymap;
    struct xkb_state* state;
    struct xkb_compose_table* composeTable;
    struct xkb_compose_state* compose;
    // The layout group in effect, and the modifiers.
    uint32_t layout;
    mwinModifiers modifiers;
} mwinXkbKeyboard;

// Makes the xkb context and the locale's compose state (LC_ALL,
// LC_CTYPE, LANG); false without the context.
bool mwinXkbStart(mwinXkbKeyboard* keyboard, const mwinXkbApi* api);

// Frees everything.
void mwinXkbStop(mwinXkbKeyboard* keyboard);

// Takes a new keymap and its state, with the layout group and modifiers
// the state has, freeing the ones before.
void mwinXkbSetKeymap(mwinXkbKeyboard* keyboard, struct xkb_keymap* keymap,
                      struct xkb_state* state);

// Takes the window system's modifier and group state; true when the
// layout group changed.
bool mwinXkbUpdateState(mwinXkbKeyboard* keyboard, uint32_t depressed, uint32_t latched,
                        uint32_t locked, uint32_t depressedGroup, uint32_t latchedGroup,
                        uint32_t lockedGroup);

// What a key by its evdev code types with no modifier in the current
// layout: its code point, or MWIN_KEY_NAMED with its code.
mwinKey mwinXkbKeyOf(const mwinXkbKeyboard* keyboard, uint32_t evdev, mwinKeyCode code);

// The same for a key code, as mwinMapKeyCode answers.
mwinKey mwinXkbMapKeyCode(const mwinXkbKeyboard* keyboard, mwinKeyCode code);

// The text a key typed as it went down: through a compose sequence
// when compose is true and one runs or starts, none for control
// characters. Returns its length in bytes.
uint32_t mwinXkbType(mwinXkbKeyboard* keyboard, uint32_t evdev, bool compose,
                     char text[MWIN_XKB_TEXT_BYTES]);

// Whether a key repeats while held, and ends a compose sequence.
bool mwinXkbRepeats(const mwinXkbKeyboard* keyboard, uint32_t evdev);
void mwinXkbResetCompose(mwinXkbKeyboard* keyboard);

// The current layout's name, as mwinGetKeyboardLayout reports it.
mwinResult mwinXkbLayoutName(const mwinXkbKeyboard* keyboard, char* buffer, size_t capacity,
                             size_t* lengthOut);

#endif // MAUL_WINDOW_SRC_XKB_KEYBOARD_H
