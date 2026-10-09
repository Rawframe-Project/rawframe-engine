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

static bool FindConnection(muiDBusApi* api)
{
    return FIND(busGetPrivate, dbus_bus_get_private) &&
           FIND(openPrivate, dbus_connection_open_private) &&
           FIND(busRegister, dbus_bus_register) && FIND(requestName, dbus_bus_request_name) &&
           FIND(addMatch, dbus_bus_add_match) &&
           FIND(setExitOnDisconnect, dbus_connection_set_exit_on_disconnect) &&
           FIND(close, dbus_connection_close) && FIND(unrefConnection, dbus_connection_unref) &&
           FIND(getUnixFd, dbus_connection_get_unix_fd) &&
           FIND(readWrite, dbus_connection_read_write) &&
           FIND(dispatch, dbus_connection_dispatch) && FIND(uniqueName, dbus_bus_get_unique_name) &&
           FIND(addFilter, dbus_connection_add_filter) &&
           FIND(removeFilter, dbus_connection_remove_filter) && FIND(send, dbus_connection_send) &&
           FIND(sendWithReply, dbus_connection_send_with_reply) &&
           FIND(sendWithReplyAndBlock, dbus_connection_send_with_reply_and_block) &&
           FIND(completed, dbus_pending_call_get_completed) &&
           FIND(stealReply, dbus_pending_call_steal_reply) &&
           FIND(cancel, dbus_pending_call_cancel) && FIND(unrefPending, dbus_pending_call_unref);
}

static bool FindMessage(muiDBusApi* api)
{
    return FIND(newMethodCall, dbus_message_new_method_call) &&
           FIND(newMethodReturn, dbus_message_new_method_return) &&
           FIND(newError, dbus_message_new_error) && FIND(newSignal, dbus_message_new_signal) &&
           FIND(unrefMessage, dbus_message_unref) && FIND(messageType, dbus_message_get_type) &&
           FIND(path, dbus_message_get_path) && FIND(interface, dbus_message_get_interface) &&
           FIND(member, dbus_message_get_member) && FIND(sender, dbus_message_get_sender) &&
           FIND(errorName, dbus_message_get_error_name) &&
           FIND(iterInitAppend, dbus_message_iter_init_append) &&
           FIND(appendBasic, dbus_message_iter_append_basic) &&
           FIND(openContainer, dbus_message_iter_open_container) &&
           FIND(closeContainer, dbus_message_iter_close_container) &&
           FIND(iterInit, dbus_message_iter_init) &&
           FIND(argType, dbus_message_iter_get_arg_type) &&
           FIND(getBasic, dbus_message_iter_get_basic) && FIND(next, dbus_message_iter_next) &&
           FIND(recurse, dbus_message_iter_recurse);
}

bool muiLoadDBus(muiDBusApi* api)
{
    memset(api, 0, sizeof(*api));
    // Never unloaded: the library keeps caches in its globals for the
    // whole process, which other libraries of the program may share.
    api->library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    return api->library != nullptr && FindConnection(api) && FindMessage(api);
}
