// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A keyboard as libxkbcommon sees it.

#include "xkb_keyboard.h"

#include "evdev.h"

#include "maul-unicode/encoding.h"

#include <stdlib.h>
#include <string.h>

// Evdev codes are xkb key codes less 8.
#define XKB_OFFSET 8

// The locale compose sequences follow, from the environment as the C
// library would read it.
static const char* Locale(void)
{
    static const char* const names[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        const char* value = getenv(names[i]);
        if (value != nullptr && value[0] != '\0')
        {
            return value;
        }
    }
    return "C";
}

bool mwinXkbStart(mwinXkbKeyboard* keyboard, const mwinXkbApi* api)
{
    *keyboard = (mwinXkbKeyboard){.api = api};
    keyboard->context = api->contextNew(XKB_CONTEXT_NO_FLAGS);
    if (keyboard->context == nullptr)
    {
        return false;
    }
    // Without a compose table for the locale, keys type what they type.
    keyboard->composeTable =
        api->composeTableNewFromLocale(keyboard->context, Locale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (keyboard->composeTable != nullptr)
    {
        keyboard->compose =
            api->composeStateNew(keyboard->composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
    }
    return true;
}

void mwinXkbStop(mwinXkbKeyboard* keyboard)
{
    const mwinXkbApi* api = keyboard->api;
    if (api == nullptr)
    {
        return;
    }
    mwinXkbSetKeymap(keyboard, nullptr, nullptr);
    if (keyboard->compose != nullptr)
    {
        api->composeStateUnref(keyboard->compose);
    }
    if (keyboard->composeTable != nullptr)
    {
        api->composeTableUnref(keyboard->composeTable);
    }
    if (keyboard->context != nullptr)
    {
        api->contextUnref(keyboard->context);
    }
    *keyboard = (mwinXkbKeyboard){0};
}

// The modifiers an xkb state has in effect.
static mwinModifiers ModifiersOf(const mwinXkbApi* api, struct xkb_state* state)
{
    static const struct
    {
        const char* name;
        mwinModifiers modifier;
    } table[] = {
        {XKB_MOD_NAME_SHIFT, mwin_modShift},   {XKB_MOD_NAME_CTRL, mwin_modControl},
        {XKB_MOD_NAME_ALT, mwin_modAlt},       {XKB_MOD_NAME_LOGO, mwin_modMeta},
        {XKB_MOD_NAME_CAPS, mwin_modCapsLock}, {XKB_MOD_NAME_NUM, mwin_modNumLock},
    };
    mwinModifiers modifiers = 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        if (api->stateModNameIsActive(state, table[i].name, XKB_STATE_MODS_EFFECTIVE) > 0)
        {
            modifiers |= table[i].modifier;
        }
    }
    return modifiers;
}

void mwinXkbSetKeymap(mwinXkbKeyboard* keyboard, struct xkb_keymap* keymap, struct xkb_state* state)
{
    const mwinXkbApi* api = keyboard->api;
    if (keyboard->state != nullptr)
    {
        api->stateUnref(keyboard->state);
    }
    if (keyboard->keymap != nullptr)
    {
        api->keymapUnref(keyboard->keymap);
    }
    keyboard->keymap = keymap;
    keyboard->state = state;
    keyboard->layout =
        state != nullptr ? api->stateSerializeLayout(state, XKB_STATE_LAYOUT_EFFECTIVE) : 0;
    keyboard->modifiers = state != nullptr ? ModifiersOf(api, state) : 0;
}

bool mwinXkbUpdateState(mwinXkbKeyboard* keyboard, uint32_t depressed, uint32_t latched,
                        uint32_t locked, uint32_t depressedGroup, uint32_t latchedGroup,
                        uint32_t lockedGroup)
{
    const mwinXkbApi* api = keyboard->api;
    if (keyboard->state == nullptr)
    {
        return false;
    }
    api->stateUpdateMask(keyboard->state, depressed, latched, locked, depressedGroup, latchedGroup,
                         lockedGroup);
    keyboard->modifiers = ModifiersOf(api, keyboard->state);
    uint32_t layout = api->stateSerializeLayout(keyboard->state, XKB_STATE_LAYOUT_EFFECTIVE);
    bool changed = layout != keyboard->layout;
    keyboard->layout = layout;
    return changed;
}

mwinKey mwinXkbKeyOf(const mwinXkbKeyboard* keyboard, uint32_t evdev, mwinKeyCode code)
{
    const xkb_keysym_t* symbols = nullptr;
    if (keyboard->keymap != nullptr &&
        keyboard->api->keymapKeyGetSymsByLevel(keyboard->keymap, evdev + XKB_OFFSET,
                                               keyboard->layout, 0, &symbols) == 1)
    {
        uint32_t character = keyboard->api->keysymToUtf32(symbols[0]);
        if (character >= 0x20u && character != 0x7Fu)
        {
            return character;
        }
    }
    return MWIN_KEY_NAMED | code;
}

mwinKey mwinXkbMapKeyCode(const mwinXkbKeyboard* keyboard, mwinKeyCode code)
{
    uint32_t evdev = mwinEvdevFromKeyCode(code);
    return evdev != 0 ? mwinXkbKeyOf(keyboard, evdev, code) : MWIN_KEY_NAMED | code;
}

// A length of text, none for control characters: Enter, Tab,
// Backspace, Escape and what Control makes of letters are keys.
static uint32_t Printable(const char* text, int length)
{
    return length > 0 && length < MWIN_XKB_TEXT_BYTES && (unsigned char)text[0] >= 0x20u &&
                   text[0] != 0x7F
               ? (uint32_t)length
               : 0;
}

uint32_t mwinXkbType(mwinXkbKeyboard* keyboard, uint32_t evdev, bool compose,
                     char text[MWIN_XKB_TEXT_BYTES])
{
    const mwinXkbApi* api = keyboard->api;
    xkb_keycode_t key = evdev + XKB_OFFSET;
    struct xkb_compose_state* state = compose ? keyboard->compose : nullptr;
    if (keyboard->state == nullptr)
    {
        return 0;
    }
    if (state != nullptr &&
        api->composeStateFeed(state, api->stateKeyGetOneSym(keyboard->state, key)) ==
            XKB_COMPOSE_FEED_ACCEPTED)
    {
        switch (api->composeStateGetStatus(state))
        {
        case XKB_COMPOSE_COMPOSING:
            return 0;
        case XKB_COMPOSE_COMPOSED:
        {
            int length = api->composeStateGetUtf8(state, text, MWIN_XKB_TEXT_BYTES);
            api->composeStateReset(state);
            return Printable(text, length);
        }
        case XKB_COMPOSE_CANCELLED:
            api->composeStateReset(state);
            return 0;
        default:
            break;
        }
    }
    return Printable(text, api->stateKeyGetUtf8(keyboard->state, key, text, MWIN_XKB_TEXT_BYTES));
}

bool mwinXkbRepeats(const mwinXkbKeyboard* keyboard, uint32_t evdev)
{
    return keyboard->keymap != nullptr &&
           keyboard->api->keymapKeyRepeats(keyboard->keymap, evdev + XKB_OFFSET) != 0;
}

void mwinXkbResetCompose(mwinXkbKeyboard* keyboard)
{
    if (keyboard->compose != nullptr)
    {
        keyboard->api->composeStateReset(keyboard->compose);
    }
}

mwinResult mwinXkbLayoutName(const mwinXkbKeyboard* keyboard, char* buffer, size_t capacity,
                             size_t* lengthOut)
{
    const char* name = keyboard->keymap != nullptr
                           ? keyboard->api->keymapLayoutGetName(keyboard->keymap, keyboard->layout)
                           : nullptr;
    size_t length = name != nullptr ? strlen(name) : 0;
    // A name that is not UTF-8 is no name.
    if (length > 0 && muniValidateUtf8(name, length).status != muni_success)
    {
        length = 0;
    }
    if (capacity > 0 && length > 0)
    {
        memcpy(buffer, name, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}
