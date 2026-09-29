// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libdbus-1.

#include "dbus_api.h"

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

mwinResult mwinLoadDBus(mwinDBusApi* api)
{
    memset(api, 0, sizeof(*api));
    // Never unloaded: the library keeps caches in its globals for the
    // whole process, which other libraries of the program may share.
    api->library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (api->library == nullptr)
    {
        return mwin_errorUnsupported;
    }
    bool found =
        FIND(busGetPrivate, dbus_bus_get_private) &&
        FIND(openPrivate, dbus_connection_open_private) && FIND(busRegister, dbus_bus_register) &&
        FIND(setExitOnDisconnect, dbus_connection_set_exit_on_disconnect) &&
        FIND(close, dbus_connection_close) && FIND(unrefConnection, dbus_connection_unref) &&
        FIND(readWrite, dbus_connection_read_write) && FIND(flush, dbus_connection_flush) &&
        FIND(dispatch, dbus_connection_dispatch) &&
        FIND(newMethodCall, dbus_message_new_method_call) &&
        FIND(unrefMessage, dbus_message_unref) &&
        FIND(iterInitAppend, dbus_message_iter_init_append) &&
        FIND(appendBasic, dbus_message_iter_append_basic) &&
        FIND(openContainer, dbus_message_iter_open_container) &&
        FIND(closeContainer, dbus_message_iter_close_container) &&
        FIND(appendFixedArray, dbus_message_iter_append_fixed_array) &&
        FIND(addFilter, dbus_connection_add_filter) &&
        FIND(removeFilter, dbus_connection_remove_filter) &&
        FIND(isSignal, dbus_message_is_signal) && FIND(path, dbus_message_get_path) &&
        FIND(uniqueName, dbus_bus_get_unique_name) && FIND(next, dbus_message_iter_next) &&
        FIND(recurse, dbus_message_iter_recurse) && FIND(send, dbus_connection_send) &&
        FIND(sendWithReply, dbus_connection_send_with_reply) &&
        FIND(completed, dbus_pending_call_get_completed) &&
        FIND(stealReply, dbus_pending_call_steal_reply) && FIND(cancel, dbus_pending_call_cancel) &&
        FIND(unrefPending, dbus_pending_call_unref) && FIND(messageType, dbus_message_get_type) &&
        FIND(errorName, dbus_message_get_error_name) && FIND(iterInit, dbus_message_iter_init) &&
        FIND(argType, dbus_message_iter_get_arg_type) &&
        FIND(getBasic, dbus_message_iter_get_basic);
    if (!found)
    {
        mwinUnloadDBus(api);
        return mwin_errorUnsupported;
    }
    return mwin_success;
}

void mwinUnloadDBus(mwinDBusApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}
