// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PulseAudio backend's connection and devices. The context owns a
// plain pa_mainloop and iterates it on the host's thread: without
// blocking when the host drains notifications, and up to a deadline at
// creation. Sinks and sources become devices, the server's defaults the
// defaults, and subscription events new queries. A server that goes
// away is reconnected on later drains.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "form.h"
#include "layout.h"
#include "pulse_core.h"
#include "pulse_stream.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

int64_t maudPulseNow(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
}

static maudPulseNode* FindNode(maudPulse* pulse, maudDirection direction, uint32_t index)
{
    for (uint32_t i = 0; i < pulse->nodeCapacity; ++i)
    {
        maudPulseNode* node = &pulse->nodes[i];
        if (node->used && node->direction == direction && node->index == index)
        {
            return node;
        }
    }
    return nullptr;
}

// Points both roles' defaults at the devices the server names, where
// those exist.
static void ResolveDefaults(maudPulse* pulse)
{
    for (uint32_t i = 0; i < pulse->nodeCapacity; ++i)
    {
        const maudPulseNode* node = &pulse->nodes[i];
        const maudDeviceSlot* slot =
            node->used ? maudFindDevice(pulse->context, node->device) : nullptr;
        const char* wanted = slot != nullptr ? pulse->server.defaultNames[node->direction] : "";
        if (slot != nullptr && strlen(wanted) == slot->key.length &&
            memcmp(wanted, slot->key.bytes, slot->key.length) == 0)
        {
            maudSetDefaultDevice(pulse->context, maud_roleGeneral, node->device);
            maudSetDefaultDevice(pulse->context, maud_roleCommunications, node->device);
        }
    }
}

// Adds a sink or source as a device, or updates the format of one
// already known.
// What a sink or source said of itself.
typedef struct Node
{
    maudDirection direction;
    uint32_t index;
    const char* name;
    const char* description;
    const pa_sample_spec* spec;
    maudDeviceForm form;
} Node;

// The form of a port type, as pa_device_port_type_t numbers them.
static maudDeviceForm FormOfPort(uint32_t type, maudDirection direction)
{
    static const char* const names[] = {
        [PA_DEVICE_PORT_TYPE_SPEAKER] = "speaker",
        [PA_DEVICE_PORT_TYPE_HEADPHONES] = "headphones",
        [PA_DEVICE_PORT_TYPE_LINE] = "line",
        [PA_DEVICE_PORT_TYPE_MIC] = "mic",
        [PA_DEVICE_PORT_TYPE_HEADSET] = "headset",
        [PA_DEVICE_PORT_TYPE_HANDSET] = "handset",
        [PA_DEVICE_PORT_TYPE_EARPIECE] = "earpiece",
        [PA_DEVICE_PORT_TYPE_SPDIF] = "spdif",
        [PA_DEVICE_PORT_TYPE_HDMI] = "hdmi",
        [PA_DEVICE_PORT_TYPE_TV] = "tv",
        [PA_DEVICE_PORT_TYPE_PORTABLE] = "portable",
        [PA_DEVICE_PORT_TYPE_HANDSFREE] = "handsfree",
        [PA_DEVICE_PORT_TYPE_CAR] = "car",
        [PA_DEVICE_PORT_TYPE_HIFI] = "hifi",
        [PA_DEVICE_PORT_TYPE_PHONE] = "phone",
        [PA_DEVICE_PORT_TYPE_ANALOG] = "analog",
    };
    return type < sizeof(names) / sizeof(names[0]) ? maudFormOfName(names[type], direction)
                                                   : maud_formUnknown;
}

// The form a sink or source leads to: its active port's type where
// libpulse has it, else its form factor property.
static maudDeviceForm FormOf(const maudPulse* pulse, maudDirection direction, uint32_t portType,
                             bool hasPort, const pa_proplist* props)
{
    maudDeviceForm form =
        pulse->portTypes && hasPort ? FormOfPort(portType, direction) : maud_formUnknown;
    if (form == maud_formUnknown && props != nullptr)
    {
        form =
            maudFormOfName(pulse->api.proplistGets(props, PA_PROP_DEVICE_FORM_FACTOR), direction);
    }
    return form;
}

static void ApplyNode(maudPulse* pulse, const Node* reported)
{
    maudDirection direction = reported->direction;
    uint32_t index = reported->index;
    const char* name = reported->name;
    const char* description = reported->description;
    maudDeviceInfo info = {
        .direction = direction,
        .nativeLayout = maudLayoutWithChannels(reported->spec->channels),
        .nativeSampleRate = reported->spec->rate,
        .minSampleRate = reported->spec->rate,
        .maxSampleRate = reported->spec->rate,
        .form = reported->form,
    };
    maudPulseNode* node = FindNode(pulse, direction, index);
    maudDeviceSlot* slot = node != nullptr ? maudFindDevice(pulse->context, node->device) : nullptr;
    if (slot != nullptr)
    {
        slot->info.nativeLayout = info.nativeLayout;
        slot->info.nativeSampleRate = info.nativeSampleRate;
        slot->info.minSampleRate = info.minSampleRate;
        slot->info.maxSampleRate = info.maxSampleRate;
        maudSetDeviceForm(pulse->context, slot, info.form);
        return;
    }
    for (uint32_t i = 0; i < pulse->nodeCapacity && node == nullptr; ++i)
    {
        node = pulse->nodes[i].used ? nullptr : &pulse->nodes[i];
    }
    if (node == nullptr || name == nullptr)
    {
        return;
    }
    const char* shown = description != nullptr ? description : name;
    maudDeviceSpec deviceSpec = {
        .info = info,
        .name = shown,
        .nameLength = maudCutUtf8(shown, pulse->context->def.limits.deviceTextBytes),
        .key = name,
        .keyLength = strlen(name),
    };
    maudDeviceId device;
    if (maudAddDevice(pulse->context, &deviceSpec, &device) == maud_success)
    {
        *node =
            (maudPulseNode){.device = device, .index = index, .direction = direction, .used = true};
        ResolveDefaults(pulse);
    }
}

static void RemoveNode(maudPulse* pulse, maudPulseNode* node)
{
    maudDeviceSlot* slot = maudFindDevice(pulse->context, node->device);
    if (slot != nullptr)
    {
        maudRemoveDevice(pulse->context, slot);
    }
    *node = (maudPulseNode){0};
}

static void ApplySink(maudPulse* pulse, const pa_sink_info* info)
{
    const pa_sink_port_info* port = info->active_port;
    ApplyNode(pulse, &(Node){
                         .direction = maud_directionOutput,
                         .index = info->index,
                         .name = info->name,
                         .description = info->description,
                         .spec = &info->sample_spec,
                         .form = FormOf(pulse, maud_directionOutput,
                                        port != nullptr && pulse->portTypes ? port->type : 0,
                                        port != nullptr, info->proplist),
                     });
}

// A sink's monitor is a source too, but not a capture device.
static void ApplySource(maudPulse* pulse, const pa_source_info* info)
{
    if (info->monitor_of_sink == PA_INVALID_INDEX)
    {
        const pa_source_port_info* port = info->active_port;
        ApplyNode(pulse, &(Node){
                             .direction = maud_directionInput,
                             .index = info->index,
                             .name = info->name,
                             .description = info->description,
                             .spec = &info->sample_spec,
                             .form = FormOf(pulse, maud_directionInput,
                                            port != nullptr && pulse->portTypes ? port->type : 0,
                                            port != nullptr, info->proplist),
                         });
    }
}

static void ApplyServer(maudPulse* pulse, const pa_server_info* info)
{
    const char* names[2] = {info->default_sink_name, info->default_source_name};
    for (int direction = 0; direction < 2; ++direction)
    {
        char* target = pulse->server.defaultNames[direction];
        size_t length = names[direction] != nullptr ? strlen(names[direction]) : 0;
        length = length < MAUD_PULSE_NAME_BYTES - 1 ? length : MAUD_PULSE_NAME_BYTES - 1;
        if (length != 0)
        {
            memcpy(target, names[direction], length);
        }
        target[length] = '\0';
    }
    ResolveDefaults(pulse);
}

// The first listing's callbacks: each counts its query answered.
static void OnSinkListed(pa_context* context, const pa_sink_info* info, int last, void* user)
{
    (void)context;
    maudPulse* pulse = user;
    if (last != 0)
    {
        pulse->server.pending--;
    }
    else if (info != nullptr)
    {
        ApplySink(pulse, info);
    }
}

static void OnSourceListed(pa_context* context, const pa_source_info* info, int last, void* user)
{
    (void)context;
    maudPulse* pulse = user;
    if (last != 0)
    {
        pulse->server.pending--;
    }
    else if (info != nullptr)
    {
        ApplySource(pulse, info);
    }
}

static void OnServerListed(pa_context* context, const pa_server_info* info, void* user)
{
    (void)context;
    maudPulse* pulse = user;
    pulse->server.pending--;
    if (info != nullptr)
    {
        ApplyServer(pulse, info);
    }
}

// The answers to queries a change event asked for.
static void OnSinkChanged(pa_context* context, const pa_sink_info* info, int last, void* user)
{
    (void)context;
    if (last == 0 && info != nullptr)
    {
        ApplySink(user, info);
    }
}

static void OnSourceChanged(pa_context* context, const pa_source_info* info, int last, void* user)
{
    (void)context;
    if (last == 0 && info != nullptr)
    {
        ApplySource(user, info);
    }
}

static void OnServerChanged(pa_context* context, const pa_server_info* info, void* user)
{
    (void)context;
    if (info != nullptr)
    {
        ApplyServer(user, info);
    }
}

// Starts a query and lets go of its operation; its callback still runs.
static void Ask(maudPulse* pulse, pa_operation* operation)
{
    if (operation != nullptr)
    {
        pulse->api.operationUnref(operation);
    }
}

static void OnSubscription(pa_context* context, pa_subscription_event_type_t event, uint32_t index,
                           void* user)
{
    maudPulse* pulse = user;
    uint32_t facility = event & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    uint32_t type = event & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    maudDirection direction =
        facility == PA_SUBSCRIPTION_EVENT_SINK ? maud_directionOutput : maud_directionInput;
    if (facility == PA_SUBSCRIPTION_EVENT_SERVER)
    {
        Ask(pulse, pulse->api.contextGetServerInfo(context, OnServerChanged, pulse));
    }
    else if (facility != PA_SUBSCRIPTION_EVENT_SINK && facility != PA_SUBSCRIPTION_EVENT_SOURCE)
    {
        return;
    }
    else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
    {
        maudPulseNode* node = FindNode(pulse, direction, index);
        if (node != nullptr)
        {
            RemoveNode(pulse, node);
        }
    }
    else if (direction == maud_directionOutput)
    {
        Ask(pulse, pulse->api.contextGetSinkInfoByIndex(context, index, OnSinkChanged, pulse));
    }
    else
    {
        Ask(pulse, pulse->api.contextGetSourceInfoByIndex(context, index, OnSourceChanged, pulse));
    }
}

// Once connected: subscribe to changes and list the server, the sinks
// and the sources.
static void StartListing(maudPulse* pulse, pa_context* context)
{
    const maudPulseApi* api = &pulse->api;
    pulse->server.ready = true;
    pulse->server.pending = 3;
    Ask(pulse, api->contextSubscribe(context,
                                     PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                         PA_SUBSCRIPTION_MASK_SERVER,
                                     nullptr, nullptr));
    Ask(pulse, api->contextGetServerInfo(context, OnServerListed, pulse));
    Ask(pulse, api->contextGetSinkInfoList(context, OnSinkListed, pulse));
    Ask(pulse, api->contextGetSourceInfoList(context, OnSourceListed, pulse));
}

// Nothing of the context may be released inside its own callback; the
// pump does it once the iteration returns.
static void OnState(pa_context* context, void* user)
{
    maudPulse* pulse = user;
    pa_context_state_t state = pulse->api.contextGetState(context);
    if (state == PA_CONTEXT_READY)
    {
        StartListing(pulse, context);
    }
    else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
    {
        pulse->server.lost = true;
    }
}

// Makes a context and starts connecting it, never spawning a server.
// False when the connection fails at once.
static bool Connect(maudPulse* pulse)
{
    maudPulseServer* server = &pulse->server;
    server->ready = false;
    server->lost = false;
    server->pending = 0;
    server->context = pulse->api.contextNew(pulse->api.mainloopGetApi(pulse->loop), "Maul Audio");
    if (server->context == nullptr)
    {
        return false;
    }
    pulse->api.contextSetStateCallback(server->context, OnState, pulse);
    pulse->api.contextSetSubscribeCallback(server->context, OnSubscription, pulse);
    return pulse->api.contextConnect(server->context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >=
           0;
}

static void DropContext(maudPulse* pulse)
{
    maudPulseServer* server = &pulse->server;
    if (server->context != nullptr)
    {
        pulse->api.contextSetStateCallback(server->context, nullptr, nullptr);
        pulse->api.contextSetSubscribeCallback(server->context, nullptr, nullptr);
        pulse->api.contextDisconnect(server->context);
        pulse->api.contextUnref(server->context);
        server->context = nullptr;
    }
    server->ready = false;
    memset(server->defaultNames, 0, sizeof(server->defaultNames));
}

// The server went away: every device with it. Runs after the loop
// iteration that reported it.
static void LoseConnection(maudPulse* pulse)
{
    for (uint32_t i = 0; i < pulse->nodeCapacity; ++i)
    {
        if (pulse->nodes[i].used)
        {
            RemoveNode(pulse, &pulse->nodes[i]);
        }
    }
    DropContext(pulse);
    pulse->server.nextAttempt = maudPulseNow() + MAUD_PULSE_RETRY_NS;
}

// Runs the loop on the calling thread until the first listing is
// answered, the connection fails, or the deadline passes.
static bool WaitForListing(maudPulse* pulse, int64_t deadline)
{
    maudPulseServer* server = &pulse->server;
    while (!(server->ready && server->pending == 0) && !server->lost)
    {
        int64_t remaining = deadline - maudPulseNow();
        if (remaining <= 0)
        {
            return false;
        }
        if (pulse->api.mainloopPrepare(pulse->loop, (int)(remaining / 1000) + 1) < 0 ||
            pulse->api.mainloopPoll(pulse->loop) < 0 ||
            pulse->api.mainloopDispatch(pulse->loop) < 0)
        {
            return false;
        }
    }
    return !server->lost;
}

static void Release(maudContext* context, maudPulse* pulse)
{
    DropContext(pulse);
    if (pulse->loop != nullptr)
    {
        pulse->api.mainloopFree(pulse->loop);
    }
    maudUnloadPulse(&pulse->api);
    maudContextRelease(context, pulse, pulse->bytes, alignof(maudPulse));
    context->native = nullptr;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t capacity = context->def.limits.devices;
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudPulse) + (size_t)capacity * sizeof(maudPulseNode) +
                   (size_t)streams * sizeof(maudPulseStream);
    maudPulse* pulse = maudContextAllocate(context, bytes, alignof(maudPulse));
    if (pulse == nullptr)
    {
        return maud_errorCapacity;
    }
    *pulse = (maudPulse){
        .context = context,
        .nodes = (maudPulseNode*)(pulse + 1),
        .nodeCapacity = capacity,
        .bytes = bytes,
    };
    memset(pulse->nodes, 0, (size_t)capacity * sizeof(maudPulseNode));
    pulse->streams = (maudPulseStream*)(pulse->nodes + capacity);
    memset(pulse->streams, 0, (size_t)streams * sizeof(maudPulseStream));
    context->native = pulse;
    if (!maudLoadPulse(&pulse->api))
    {
        Release(context, pulse);
        return maud_errorUnsupported;
    }
    // A port's type is a field libpulse 14.0 added to its structures.
    const char* version = pulse->api.getLibraryVersion();
    pulse->portTypes = version != nullptr && strtol(version, nullptr, 10) >= 14;
    pulse->loop = pulse->api.mainloopNew();
    if (pulse->loop == nullptr || !Connect(pulse) ||
        !WaitForListing(pulse, maudPulseNow() + MAUD_PULSE_DEADLINE_NS))
    {
        Release(context, pulse);
        return maud_errorUnsupported;
    }
    return maud_success;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

static void Reconnect(maudPulse* pulse)
{
    if (maudPulseNow() < pulse->server.nextAttempt)
    {
        return;
    }
    if (!Connect(pulse))
    {
        DropContext(pulse);
        pulse->server.lost = true;
        pulse->server.nextAttempt = maudPulseNow() + MAUD_PULSE_RETRY_NS;
    }
}

static void Pump(maudContext* context)
{
    maudPulse* pulse = context->native;
    maudPulseServer* server = &pulse->server;
    if (server->context == nullptr)
    {
        Reconnect(pulse);
    }
    for (int i = 0; i < MAUD_PULSE_PUMP_ITERATIONS && !server->lost; ++i)
    {
        if (pulse->api.mainloopIterate(pulse->loop, 0, nullptr) <= 0)
        {
            break;
        }
    }
    if (server->lost && server->context != nullptr)
    {
        LoseConnection(pulse);
    }
    if (server->ready && server->pending == 0)
    {
        maudPulseResumeStreams(context);
    }
}

// PulseAudio converts each stream to its sink's rate, so a native
// stream takes the device's rate; with no device yet, the fallback.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t native = device != nullptr ? device->nativeSampleRate : MAUD_PULSE_FALLBACK_RATE;
    if (def->mode == maud_modePull ||
        (def->ratePolicy == maud_rateRequired && def->sampleRate != native))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? native : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_pulse = {
    .kind = maud_backendPulse,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = maudPulseAttachStream,
    .detachStream = maudPulseDetachStream,
    .setStreamActive = maudPulseSetStreamActive,
    .retargetStream = maudPulseRetargetStream,
    .rendersOnCaller = false,
};

const maudBackend* maudGetPulseBackend(void)
{
    return &s_pulse;
}
