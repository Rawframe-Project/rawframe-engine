// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libxkbcommon.

#include "xkb_api.h"

#include <dlfcn.h>
#include <string.h>

// A function of the library by name. The pointer comes back as data
// from dlsym and is copied, not cast, into the function pointer.
static bool Find(void* library, const char* name, void* function, size_t size)
{
    void* symbol = dlsym(library, name);
    if (symbol == nullptr)
    {
        return false;
    }
    memcpy(function, (const void*)&symbol, size);
    return true;
}

#define FIND(field, name) Find(api->library, #name, (void*)&api->field, sizeof(api->field))

mwinResult mwinLoadXkb(mwinXkbApi* api)
{
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libxkbcommon.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return mwin_errorUnsupported;
    }
    bool found = FIND(contextNew, xkb_context_new) && FIND(contextUnref, xkb_context_unref) &&
                 FIND(keymapNewFromBuffer, xkb_keymap_new_from_buffer) &&
                 FIND(keymapUnref, xkb_keymap_unref) &&
                 FIND(keymapKeyRepeats, xkb_keymap_key_repeats) &&
                 FIND(keymapLayoutGetName, xkb_keymap_layout_get_name) &&
                 FIND(keymapKeyGetSymsByLevel, xkb_keymap_key_get_syms_by_level) &&
                 FIND(stateNew, xkb_state_new) && FIND(stateUnref, xkb_state_unref) &&
                 FIND(stateUpdateMask, xkb_state_update_mask) &&
                 FIND(stateKeyGetOneSym, xkb_state_key_get_one_sym) &&
                 FIND(stateKeyGetUtf8, xkb_state_key_get_utf8) &&
                 FIND(stateModNameIsActive, xkb_state_mod_name_is_active) &&
                 FIND(stateSerializeLayout, xkb_state_serialize_layout) &&
                 FIND(keysymToUtf32, xkb_keysym_to_utf32) &&
                 FIND(composeTableNewFromLocale, xkb_compose_table_new_from_locale) &&
                 FIND(composeTableUnref, xkb_compose_table_unref) &&
                 FIND(composeStateNew, xkb_compose_state_new) &&
                 FIND(composeStateUnref, xkb_compose_state_unref) &&
                 FIND(composeStateFeed, xkb_compose_state_feed) &&
                 FIND(composeStateReset, xkb_compose_state_reset) &&
                 FIND(composeStateGetStatus, xkb_compose_state_get_status) &&
                 FIND(composeStateGetUtf8, xkb_compose_state_get_utf8);
    if (!found)
    {
        mwinUnloadXkb(api);
        return mwin_errorUnsupported;
    }
    return mwin_success;
}

void mwinUnloadXkb(mwinXkbApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}
