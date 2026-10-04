// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libpulse, opened at run time. Every function the backend calls goes
// through this table, so the library links nothing of PulseAudio's and
// runs where it is missing.

#ifndef MAUL_AUDIO_SRC_PULSE_API_H
#define MAUL_AUDIO_SRC_PULSE_API_H

#include <pulse/pulseaudio.h>
#include <stdbool.h>

typedef struct maudPulseApi
{
    void* library;
    pa_mainloop* (*mainloopNew)(void);
    void (*mainloopFree)(pa_mainloop* loop);
    pa_mainloop_api* (*mainloopGetApi)(pa_mainloop* loop);
    int (*mainloopIterate)(pa_mainloop* loop, int block, int* result);
    int (*mainloopPrepare)(pa_mainloop* loop, int timeout);
    int (*mainloopPoll)(pa_mainloop* loop);
    int (*mainloopDispatch)(pa_mainloop* loop);
    void (*mainloopWakeup)(pa_mainloop* loop);
    pa_context* (*contextNew)(pa_mainloop_api* api, const char* name);
    void (*contextUnref)(pa_context* context);
    int (*contextConnect)(pa_context* context, const char* server, pa_context_flags_t flags,
                          const pa_spawn_api* api);
    void (*contextDisconnect)(pa_context* context);
    pa_context_state_t (*contextGetState)(const pa_context* context);
    void (*contextSetStateCallback)(pa_context* context, pa_context_notify_cb_t callback,
                                    void* user);
    void (*contextSetSubscribeCallback)(pa_context* context, pa_context_subscribe_cb_t callback,
                                        void* user);
    pa_operation* (*contextSubscribe)(pa_context* context, pa_subscription_mask_t mask,
                                      pa_context_success_cb_t callback, void* user);
    pa_operation* (*contextGetServerInfo)(pa_context* context, pa_server_info_cb_t callback,
                                          void* user);
    pa_operation* (*contextGetSinkInfoList)(pa_context* context, pa_sink_info_cb_t callback,
                                            void* user);
    pa_operation* (*contextGetSourceInfoList)(pa_context* context, pa_source_info_cb_t callback,
                                              void* user);
    pa_operation* (*contextGetSinkInfoByIndex)(pa_context* context, uint32_t index,
                                               pa_sink_info_cb_t callback, void* user);
    pa_operation* (*contextGetSourceInfoByIndex)(pa_context* context, uint32_t index,
                                                 pa_source_info_cb_t callback, void* user);
    void (*operationUnref)(pa_operation* operation);
    pa_stream* (*streamNew)(pa_context* context, const char* name, const pa_sample_spec* spec,
                            const pa_channel_map* map);
    void (*streamUnref)(pa_stream* stream);
    int (*streamConnectPlayback)(pa_stream* stream, const char* device, const pa_buffer_attr* attr,
                                 pa_stream_flags_t flags, const pa_cvolume* volume,
                                 pa_stream* sync);
    int (*streamConnectRecord)(pa_stream* stream, const char* device, const pa_buffer_attr* attr,
                               pa_stream_flags_t flags);
    int (*streamDisconnect)(pa_stream* stream);
    pa_stream_state_t (*streamGetState)(const pa_stream* stream);
    void (*streamSetWriteCallback)(pa_stream* stream, pa_stream_request_cb_t callback, void* user);
    void (*streamSetReadCallback)(pa_stream* stream, pa_stream_request_cb_t callback, void* user);
    void (*streamSetUnderflowCallback)(pa_stream* stream, pa_stream_notify_cb_t callback,
                                       void* user);
    void (*streamSetOverflowCallback)(pa_stream* stream, pa_stream_notify_cb_t callback,
                                      void* user);
    int (*streamBeginWrite)(pa_stream* stream, void** data, size_t* bytes);
    int (*streamWrite)(pa_stream* stream, const void* data, size_t bytes, pa_free_cb_t free,
                       int64_t offset, pa_seek_mode_t seek);
    int (*streamPeek)(pa_stream* stream, const void** data, size_t* bytes);
    int (*streamDrop)(pa_stream* stream);
    int (*streamGetLatency)(pa_stream* stream, pa_usec_t* latency, int* negative);
    int (*streamCancelWrite)(pa_stream* stream);
    pa_operation* (*streamCork)(pa_stream* stream, int cork, pa_stream_success_cb_t callback,
                                void* user);
    const char* (*proplistGets)(const pa_proplist* list, const char* key);
    const char* (*getLibraryVersion)(void);
} maudPulseApi;

// Opens libpulse and fills the table. False, with the table zeroed,
// when the library or a function is missing.
bool maudLoadPulse(maudPulseApi* api);

// Closes what maudLoadPulse opened.
void maudUnloadPulse(maudPulseApi* api);

#endif // MAUL_AUDIO_SRC_PULSE_API_H
