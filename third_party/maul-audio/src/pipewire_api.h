// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libpipewire, opened at run time. The headers' inline calls go
// through PipeWire's method tables; the exported functions the backend
// uses go through this table, so the library links nothing of
// PipeWire's and runs where it is missing.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_API_H
#define MAUL_AUDIO_SRC_PIPEWIRE_API_H

#include <pipewire/pipewire.h>
#include <stdbool.h>

typedef struct maudPipewireApi
{
    void* library;
    void (*init)(int* argc, char** argv[]);
    void (*deinit)(void);
    struct pw_loop* (*loopNew)(const struct spa_dict* props);
    void (*loopDestroy)(struct pw_loop* loop);
    struct pw_context* (*contextNew)(struct pw_loop* mainLoop, struct pw_properties* props,
                                     size_t userDataSize);
    void (*contextDestroy)(struct pw_context* context);
    struct pw_core* (*contextConnect)(struct pw_context* context, struct pw_properties* props,
                                      size_t userDataSize);
    int (*coreDisconnect)(struct pw_core* core);
    void (*proxyDestroy)(struct pw_proxy* proxy);
    struct pw_properties* (*propertiesNew)(const char* key, ...);
    int (*propertiesSetf)(struct pw_properties* properties, const char* key, const char* format,
                          ...);
    struct pw_stream* (*streamNew)(struct pw_core* core, const char* name,
                                   struct pw_properties* props);
    void (*streamDestroy)(struct pw_stream* stream);
    void (*streamAddListener)(struct pw_stream* stream, struct spa_hook* listener,
                              const struct pw_stream_events* events, void* data);
    int (*streamConnect)(struct pw_stream* stream, enum pw_direction direction, uint32_t targetId,
                         enum pw_stream_flags flags, const struct spa_pod** params,
                         uint32_t paramCount);
    int (*streamUpdateParams)(struct pw_stream* stream, const struct spa_pod** params,
                              uint32_t paramCount);
    int (*streamSetActive)(struct pw_stream* stream, bool active);
    struct pw_buffer* (*streamDequeueBuffer)(struct pw_stream* stream);
    int (*streamQueueBuffer)(struct pw_stream* stream, struct pw_buffer* buffer);
    int (*streamGetTime)(struct pw_stream* stream, struct pw_time* time, size_t size);
} maudPipewireApi;

// Opens libpipewire and fills the table. False, with the table zeroed,
// when the library or a function is missing.
bool maudLoadPipewire(maudPipewireApi* api);

// Closes what maudLoadPipewire opened.
void maudUnloadPipewire(maudPipewireApi* api);

#endif // MAUL_AUDIO_SRC_PIPEWIRE_API_H
