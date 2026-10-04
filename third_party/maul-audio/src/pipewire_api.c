// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libpipewire. The library is reference counted by the dynamic
// loader, so each context opens and closes it on its own. Every
// function of the table has one entry below, and the build fails when
// one is missing.

#include "pipewire_api.h"

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

typedef struct ApiEntry
{
    const char* name;
    size_t offset;
} ApiEntry;

#define ENTRY(field, name) {name, offsetof(maudPipewireApi, field)}

static const ApiEntry s_entries[] = {
    ENTRY(init, "pw_init"),
    ENTRY(deinit, "pw_deinit"),
    ENTRY(loopNew, "pw_loop_new"),
    ENTRY(loopDestroy, "pw_loop_destroy"),
    ENTRY(contextNew, "pw_context_new"),
    ENTRY(contextDestroy, "pw_context_destroy"),
    ENTRY(contextConnect, "pw_context_connect"),
    ENTRY(coreDisconnect, "pw_core_disconnect"),
    ENTRY(proxyDestroy, "pw_proxy_destroy"),
    ENTRY(propertiesNew, "pw_properties_new"),
    ENTRY(propertiesSetf, "pw_properties_setf"),
    ENTRY(streamNew, "pw_stream_new"),
    ENTRY(streamDestroy, "pw_stream_destroy"),
    ENTRY(streamAddListener, "pw_stream_add_listener"),
    ENTRY(streamConnect, "pw_stream_connect"),
    ENTRY(streamUpdateParams, "pw_stream_update_params"),
    ENTRY(streamSetActive, "pw_stream_set_active"),
    ENTRY(streamDequeueBuffer, "pw_stream_dequeue_buffer"),
    ENTRY(streamQueueBuffer, "pw_stream_queue_buffer"),
    ENTRY(streamGetTime, "pw_stream_get_time_n"),
};

// The table holds the library handle and then only function pointers.
static_assert(sizeof(s_entries) / sizeof(s_entries[0]) ==
                  (sizeof(maudPipewireApi) - offsetof(maudPipewireApi, init)) /
                      sizeof(void (*)(void)),
              "every function of maudPipewireApi has an entry");

bool maudLoadPipewire(maudPipewireApi* api)
{
    *api = (maudPipewireApi){0};
    api->library = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); ++i)
    {
        void* function = dlsym(api->library, s_entries[i].name);
        if (function == nullptr)
        {
            maudUnloadPipewire(api);
            return false;
        }
        // The loader returns an object pointer; the slot has the
        // function's pointer type and the same size on every platform
        // with dlopen.
        memcpy((char*)api + s_entries[i].offset, (const void*)&function, sizeof(function));
    }
    return true;
}

void maudUnloadPipewire(maudPipewireApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    *api = (maudPipewireApi){0};
}
