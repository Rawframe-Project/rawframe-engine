// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libpulse. The library is reference counted by the dynamic
// loader, so each context opens and closes it on its own. Every
// function of the table has one entry below, and the build fails when
// one is missing.

#include "pulse_api.h"

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

typedef struct ApiEntry
{
    const char* name;
    size_t offset;
} ApiEntry;

#define ENTRY(field, name) {name, offsetof(maudPulseApi, field)}

static const ApiEntry s_entries[] = {
    ENTRY(mainloopNew, "pa_mainloop_new"),
    ENTRY(mainloopFree, "pa_mainloop_free"),
    ENTRY(mainloopGetApi, "pa_mainloop_get_api"),
    ENTRY(mainloopIterate, "pa_mainloop_iterate"),
    ENTRY(mainloopPrepare, "pa_mainloop_prepare"),
    ENTRY(mainloopPoll, "pa_mainloop_poll"),
    ENTRY(mainloopDispatch, "pa_mainloop_dispatch"),
    ENTRY(mainloopWakeup, "pa_mainloop_wakeup"),
    ENTRY(contextNew, "pa_context_new"),
    ENTRY(contextUnref, "pa_context_unref"),
    ENTRY(contextConnect, "pa_context_connect"),
    ENTRY(contextDisconnect, "pa_context_disconnect"),
    ENTRY(contextGetState, "pa_context_get_state"),
    ENTRY(contextSetStateCallback, "pa_context_set_state_callback"),
    ENTRY(contextSetSubscribeCallback, "pa_context_set_subscribe_callback"),
    ENTRY(contextSubscribe, "pa_context_subscribe"),
    ENTRY(contextGetServerInfo, "pa_context_get_server_info"),
    ENTRY(contextGetSinkInfoList, "pa_context_get_sink_info_list"),
    ENTRY(contextGetSourceInfoList, "pa_context_get_source_info_list"),
    ENTRY(contextGetSinkInfoByIndex, "pa_context_get_sink_info_by_index"),
    ENTRY(contextGetSourceInfoByIndex, "pa_context_get_source_info_by_index"),
    ENTRY(operationUnref, "pa_operation_unref"),
    ENTRY(streamNew, "pa_stream_new"),
    ENTRY(streamUnref, "pa_stream_unref"),
    ENTRY(streamConnectPlayback, "pa_stream_connect_playback"),
    ENTRY(streamConnectRecord, "pa_stream_connect_record"),
    ENTRY(streamDisconnect, "pa_stream_disconnect"),
    ENTRY(streamGetState, "pa_stream_get_state"),
    ENTRY(streamSetWriteCallback, "pa_stream_set_write_callback"),
    ENTRY(streamSetReadCallback, "pa_stream_set_read_callback"),
    ENTRY(streamSetUnderflowCallback, "pa_stream_set_underflow_callback"),
    ENTRY(streamSetOverflowCallback, "pa_stream_set_overflow_callback"),
    ENTRY(streamBeginWrite, "pa_stream_begin_write"),
    ENTRY(streamWrite, "pa_stream_write"),
    ENTRY(streamPeek, "pa_stream_peek"),
    ENTRY(streamDrop, "pa_stream_drop"),
    ENTRY(streamGetLatency, "pa_stream_get_latency"),
    ENTRY(streamCancelWrite, "pa_stream_cancel_write"),
    ENTRY(streamCork, "pa_stream_cork"),
    ENTRY(proplistGets, "pa_proplist_gets"),
    ENTRY(getLibraryVersion, "pa_get_library_version"),
};

// The table holds the library handle and then only function pointers.
static_assert(sizeof(s_entries) / sizeof(s_entries[0]) ==
                  (sizeof(maudPulseApi) - offsetof(maudPulseApi, mainloopNew)) /
                      sizeof(void (*)(void)),
              "every function of maudPulseApi has an entry");

bool maudLoadPulse(maudPulseApi* api)
{
    *api = (maudPulseApi){0};
    api->library = dlopen("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); ++i)
    {
        void* function = dlsym(api->library, s_entries[i].name);
        if (function == nullptr)
        {
            maudUnloadPulse(api);
            return false;
        }
        // The loader returns an object pointer; the slot has the
        // function's pointer type and the same size on every platform
        // with dlopen.
        memcpy((char*)api + s_entries[i].offset, (const void*)&function, sizeof(function));
    }
    return true;
}

void maudUnloadPulse(maudPulseApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    *api = (maudPulseApi){0};
}
