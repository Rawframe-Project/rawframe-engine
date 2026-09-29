// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libxkbcommon, opened at run time (W7) into a table each context
// loads for itself: keymaps, keyboard state and compose sequences for
// the Wayland backend, and later the X11 one.

#ifndef MAUL_WINDOW_SRC_XKB_API_H
#define MAUL_WINDOW_SRC_XKB_API_H

#include "maul-window/base.h"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

typedef struct mwinXkbApi
{
    void* library;
    typeof(xkb_context_new)* contextNew;
    typeof(xkb_context_unref)* contextUnref;
    typeof(xkb_keymap_new_from_buffer)* keymapNewFromBuffer;
    typeof(xkb_keymap_unref)* keymapUnref;
    typeof(xkb_keymap_key_repeats)* keymapKeyRepeats;
    typeof(xkb_keymap_layout_get_name)* keymapLayoutGetName;
    typeof(xkb_keymap_key_get_syms_by_level)* keymapKeyGetSymsByLevel;
    typeof(xkb_state_new)* stateNew;
    typeof(xkb_state_unref)* stateUnref;
    typeof(xkb_state_update_mask)* stateUpdateMask;
    typeof(xkb_state_key_get_one_sym)* stateKeyGetOneSym;
    typeof(xkb_state_key_get_utf8)* stateKeyGetUtf8;
    typeof(xkb_state_mod_name_is_active)* stateModNameIsActive;
    typeof(xkb_state_serialize_layout)* stateSerializeLayout;
    typeof(xkb_keysym_to_utf32)* keysymToUtf32;
    typeof(xkb_compose_table_new_from_locale)* composeTableNewFromLocale;
    typeof(xkb_compose_table_unref)* composeTableUnref;
    typeof(xkb_compose_state_new)* composeStateNew;
    typeof(xkb_compose_state_unref)* composeStateUnref;
    typeof(xkb_compose_state_feed)* composeStateFeed;
    typeof(xkb_compose_state_reset)* composeStateReset;
    typeof(xkb_compose_state_get_status)* composeStateGetStatus;
    typeof(xkb_compose_state_get_utf8)* composeStateGetUtf8;
} mwinXkbApi;

// Opens libxkbcommon and fills the table: mwin_errorUnsupported when the
// library or a function is missing.
mwinResult mwinLoadXkb(mwinXkbApi* api);

// Closes what mwinLoadXkb opened.
void mwinUnloadXkb(mwinXkbApi* api);

#endif // MAUL_WINDOW_SRC_XKB_API_H
