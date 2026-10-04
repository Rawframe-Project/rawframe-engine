// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire backend's connection and devices. The context owns a
// pw_loop as PipeWire's main loop and iterates it on the host's thread:
// without blocking when the host drains notifications, and up to a
// deadline for the round trips of creation. Registry events become
// device table changes; the default metadata names the defaults.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "follow.h"
#include "form.h"
#include "layout.h"
#include "pipewire_card.h"
#include "pipewire_core.h"
#include "pipewire_stream.h"

#include <errno.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/raw.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/pod/iter.h>
#include <spa/utils/json.h>
#include <spa/utils/string.h>
#include <string.h>
#include <time.h>

static uint32_t PropertyNumber(const struct spa_dict* props, const char* key)
{
    const char* text = spa_dict_lookup(props, key);
    uint32_t value = 0;
    return text != nullptr && spa_atou32(text, &value, 10) ? value : 0;
}

static maudPipewireNode* FindNode(maudPipewire* pipewire, uint32_t globalId)
{
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        if (pipewire->nodes[i].used && pipewire->nodes[i].globalId == globalId)
        {
            return &pipewire->nodes[i];
        }
    }
    return nullptr;
}

// Points both roles' defaults at the devices the metadata names, where
// those exist.
static void ResolveDefaults(maudPipewire* pipewire)
{
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        const maudPipewireNode* node = &pipewire->nodes[i];
        const maudDeviceSlot* slot =
            node->used ? maudFindDevice(pipewire->context, node->device) : nullptr;
        if (slot == nullptr)
        {
            continue;
        }
        const char* wanted = pipewire->defaults.names[slot->info.direction];
        if (strlen(wanted) == slot->key.length &&
            memcmp(wanted, slot->key.bytes, slot->key.length) == 0)
        {
            maudSetDefaultDevice(pipewire->context, maud_roleGeneral, node->device);
            maudSetDefaultDevice(pipewire->context, maud_roleCommunications, node->device);
        }
    }
}

// Reads a rate or a channel count from a format param property: a
// plain value, or a choice whose default is the first value and whose
// other values bound it.
static void ReadChoice(const struct spa_pod* value, uint32_t* defaultOut, uint32_t* minOut,
                       uint32_t* maxOut)
{
    uint32_t count = 0;
    uint32_t choice = 0;
    const struct spa_pod* values = spa_pod_get_values(value, &count, &choice);
    if (values->type != SPA_TYPE_Int || count == 0)
    {
        return;
    }
    const int32_t* numbers = SPA_POD_BODY_CONST(values);
    *defaultOut = (uint32_t)numbers[0];
    *minOut = *defaultOut;
    *maxOut = *defaultOut;
    for (uint32_t i = 1; i < count; ++i)
    {
        uint32_t number = (uint32_t)numbers[i];
        *minOut = number < *minOut ? number : *minOut;
        *maxOut = number > *maxOut ? number : *maxOut;
    }
}

static void OnNodeParam(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                        const struct spa_pod* param)
{
    (void)seq;
    (void)index;
    (void)next;
    maudPipewireNode* node = data;
    maudDeviceSlot* slot = maudFindDevice(node->owner->context, node->device);
    if (id != SPA_PARAM_EnumFormat || param == nullptr || slot == nullptr)
    {
        return;
    }
    const struct spa_pod_prop* channels =
        spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_channels);
    if (channels != nullptr)
    {
        uint32_t count = 0;
        uint32_t least = 0;
        uint32_t most = 0;
        ReadChoice(&channels->value, &count, &least, &most);
        slot->info.nativeLayout = maudLayoutWithChannels(count);
    }
}

// Adds a node's device once its info brings its full properties, its
// form factor among them, and subscribes to its formats.
static void AddDeviceOf(maudPipewireNode* node, const struct spa_dict* props)
{
    maudPipewire* pipewire = node->owner;
    const char* key = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (key == nullptr)
    {
        return;
    }
    name = name != nullptr ? name : key;
    uint32_t rate = pipewire->clock.graphRate;
    // Its card's route, when it is on one, says more than its form factor.
    node->factorForm =
        maudFormOfName(spa_dict_lookup(props, PW_KEY_DEVICE_FORM_FACTOR), node->direction);
    uint32_t cardId = 0;
    const char* card = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
    const char* profileDevice = spa_dict_lookup(props, "card.profile.device");
    node->hasCard = card != nullptr && profileDevice != nullptr && spa_atou32(card, &cardId, 10) &&
                    spa_atoi32(profileDevice, &node->profileDevice, 10);
    node->cardId = cardId;
    maudDeviceSpec spec = {
        .info = {.direction = node->direction,
                 .nativeLayout =
                     maudLayoutWithChannels(PropertyNumber(props, PW_KEY_AUDIO_CHANNELS)),
                 .nativeSampleRate = rate,
                 .minSampleRate = rate,
                 .maxSampleRate = rate,
                 .form = maudPipewireNodeForm(pipewire, node)},
        .name = name,
        .nameLength = maudCutUtf8(name, pipewire->context->def.limits.deviceTextBytes),
        .key = key,
        .keyLength = strlen(key),
    };
    if (maudAddDevice(pipewire->context, &spec, &node->device) != maud_success)
    {
        node->device = (maudDeviceId){0, 0};
        return;
    }
    uint32_t ids[] = {SPA_PARAM_EnumFormat};
    pw_node_subscribe_params((struct pw_node*)node->proxy, ids, 1);
    ResolveDefaults(pipewire);
}

// The node's properties, which its registry global does not carry in
// full: the first add its device; a later form factor is a route change.
static void OnNodeInfo(void* data, const struct pw_node_info* info)
{
    maudPipewireNode* node = data;
    if (info->props == nullptr || (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) == 0)
    {
        return;
    }
    if (node->device.index1 == 0)
    {
        AddDeviceOf(node, info->props);
        return;
    }
    maudDeviceSlot* slot = maudFindDevice(node->owner->context, node->device);
    if (slot != nullptr)
    {
        node->factorForm = maudFormOfName(spa_dict_lookup(info->props, PW_KEY_DEVICE_FORM_FACTOR),
                                          node->direction);
        maudSetDeviceForm(node->owner->context, slot, maudPipewireNodeForm(node->owner, node));
    }
}

static const struct pw_node_events s_nodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = OnNodeInfo,
    .param = OnNodeParam,
};

// Binds a sink or source node; its device is added when its info
// arrives.
static void AddNode(maudPipewire* pipewire, uint32_t globalId, maudDirection direction)
{
    maudPipewireNode* node = nullptr;
    for (uint32_t i = 0; i < pipewire->nodeCapacity && node == nullptr; ++i)
    {
        node = pipewire->nodes[i].used ? nullptr : &pipewire->nodes[i];
    }
    if (node == nullptr)
    {
        return;
    }
    *node = (maudPipewireNode){
        .owner = pipewire, .globalId = globalId, .direction = direction, .used = true};
    node->proxy = pw_registry_bind(pipewire->connection.registry, globalId, PW_TYPE_INTERFACE_Node,
                                   PW_VERSION_NODE, 0);
    if (node->proxy == nullptr)
    {
        *node = (maudPipewireNode){0};
        return;
    }
    pw_node_add_listener((struct pw_node*)node->proxy, &node->listener, &s_nodeEvents, node);
}

static void RemoveNode(maudPipewire* pipewire, maudPipewireNode* node)
{
    if (node->proxy != nullptr)
    {
        spa_hook_remove(&node->listener);
        pipewire->api.proxyDestroy(node->proxy);
    }
    maudDeviceSlot* slot = maudFindDevice(pipewire->context, node->device);
    if (slot != nullptr)
    {
        maudRemoveDevice(pipewire->context, slot);
    }
    *node = (maudPipewireNode){0};
}

// Reads the node name out of a default metadata value,
// {"name": "..."}, into name; empty when there is none.
static void ParseDefaultName(const char* value, char* name)
{
    name[0] = '\0';
    struct spa_json root;
    struct spa_json object;
    if (value == nullptr)
    {
        return;
    }
    spa_json_init(&root, value, strlen(value));
    if (spa_json_enter_object(&root, &object) <= 0)
    {
        return;
    }
    char key[16];
    while (spa_json_get_string(&object, key, sizeof(key)) > 0)
    {
        if (spa_streq(key, "name"))
        {
            if (spa_json_get_string(&object, name, MAUD_PIPEWIRE_NAME_BYTES) <= 0)
            {
                name[0] = '\0';
            }
            return;
        }
        const char* skipped;
        if (spa_json_next(&object, &skipped) <= 0)
        {
            return;
        }
    }
}

static int OnMetadataProperty(void* data, uint32_t subject, const char* key, const char* type,
                              const char* value)
{
    (void)type;
    maudPipewire* pipewire = data;
    if (subject != PW_ID_CORE || key == nullptr)
    {
        return 0;
    }
    if (spa_streq(key, "default.audio.sink"))
    {
        ParseDefaultName(value, pipewire->defaults.names[maud_directionOutput]);
    }
    else if (spa_streq(key, "default.audio.source"))
    {
        ParseDefaultName(value, pipewire->defaults.names[maud_directionInput]);
    }
    else
    {
        return 0;
    }
    ResolveDefaults(pipewire);
    return 0;
}

static const struct pw_metadata_events s_metadataEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = OnMetadataProperty,
};

// Gives every device the graph's rate and lets native streams follow
// it when it changed.
static void UpdateGraphRate(maudPipewire* pipewire)
{
    maudPipewireClock* clock = &pipewire->clock;
    uint32_t rate = clock->forceRate != 0   ? clock->forceRate
                    : clock->clockRate != 0 ? clock->clockRate
                                            : MAUD_PIPEWIRE_FALLBACK_RATE;
    if (rate == clock->graphRate)
    {
        return;
    }
    clock->graphRate = rate;
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        maudDeviceSlot* slot = pipewire->nodes[i].used
                                   ? maudFindDevice(pipewire->context, pipewire->nodes[i].device)
                                   : nullptr;
        if (slot != nullptr)
        {
            slot->info.nativeSampleRate = rate;
            slot->info.minSampleRate = rate;
            slot->info.maxSampleRate = rate;
        }
    }
    maudRefreshNativeRates(pipewire->context);
}

static int OnSettingsProperty(void* data, uint32_t subject, const char* key, const char* type,
                              const char* value)
{
    (void)type;
    maudPipewire* pipewire = data;
    uint32_t number = 0;
    if (subject != PW_ID_CORE || key == nullptr ||
        (value != nullptr && !spa_atou32(value, &number, 10)))
    {
        return 0;
    }
    if (spa_streq(key, "clock.rate"))
    {
        pipewire->clock.clockRate = number;
    }
    else if (spa_streq(key, "clock.force-rate"))
    {
        pipewire->clock.forceRate = number;
    }
    else
    {
        return 0;
    }
    UpdateGraphRate(pipewire);
    return 0;
}

static const struct pw_metadata_events s_settingsEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = OnSettingsProperty,
};

static void BindSettings(maudPipewire* pipewire, uint32_t globalId)
{
    maudPipewireClock* clock = &pipewire->clock;
    clock->metadata = pw_registry_bind(pipewire->connection.registry, globalId,
                                       PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0);
    if (clock->metadata != nullptr)
    {
        pw_metadata_add_listener((struct pw_metadata*)clock->metadata, &clock->listener,
                                 &s_settingsEvents, pipewire);
    }
}

static void BindDefaults(maudPipewire* pipewire, uint32_t globalId)
{
    maudPipewireDefaults* defaults = &pipewire->defaults;
    defaults->metadata = pw_registry_bind(pipewire->connection.registry, globalId,
                                          PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0);
    if (defaults->metadata != nullptr)
    {
        pw_metadata_add_listener((struct pw_metadata*)defaults->metadata, &defaults->listener,
                                 &s_metadataEvents, pipewire);
    }
}

static void OnGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                     uint32_t version, const struct spa_dict* props)
{
    (void)permissions;
    (void)version;
    maudPipewire* pipewire = data;
    if (props == nullptr)
    {
        return;
    }
    if (spa_streq(type, PW_TYPE_INTERFACE_Node))
    {
        const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if (spa_streq(mediaClass, "Audio/Sink"))
        {
            AddNode(pipewire, id, maud_directionOutput);
        }
        else if (mediaClass != nullptr && spa_strstartswith(mediaClass, "Audio/Source"))
        {
            AddNode(pipewire, id, maud_directionInput);
        }
    }
    else if (spa_streq(type, PW_TYPE_INTERFACE_Device) &&
             spa_streq(spa_dict_lookup(props, PW_KEY_MEDIA_CLASS), "Audio/Device"))
    {
        maudPipewireAddCard(pipewire, id);
    }
    else if (spa_streq(type, PW_TYPE_INTERFACE_Metadata))
    {
        const char* name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (pipewire->defaults.metadata == nullptr && spa_streq(name, "default"))
        {
            BindDefaults(pipewire, id);
        }
        else if (pipewire->clock.metadata == nullptr && spa_streq(name, "settings"))
        {
            BindSettings(pipewire, id);
        }
    }
}

static void OnGlobalRemove(void* data, uint32_t id)
{
    maudPipewire* pipewire = data;
    maudPipewireNode* node = FindNode(pipewire, id);
    if (node != nullptr)
    {
        RemoveNode(pipewire, node);
    }
    else
    {
        (void)maudPipewireRemoveCard(pipewire, id);
    }
}

static const struct pw_registry_events s_registryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = OnGlobal,
    .global_remove = OnGlobalRemove,
};

// The daemon went away: every device with it.
// Destroys what belongs to the core, which goes with it: the metadata,
// the registry and the core itself. The nodes are removed before.
static void DropCore(maudPipewire* pipewire)
{
    maudPipewireConnection* connection = &pipewire->connection;
    if (pipewire->defaults.metadata != nullptr)
    {
        spa_hook_remove(&pipewire->defaults.listener);
        pipewire->api.proxyDestroy(pipewire->defaults.metadata);
    }
    if (pipewire->clock.metadata != nullptr)
    {
        spa_hook_remove(&pipewire->clock.listener);
        pipewire->api.proxyDestroy(pipewire->clock.metadata);
    }
    pipewire->defaults = (maudPipewireDefaults){0};
    pipewire->clock.metadata = nullptr;
    if (connection->registry != nullptr)
    {
        spa_hook_remove(&connection->registryListener);
        pipewire->api.proxyDestroy((struct pw_proxy*)connection->registry);
        connection->registry = nullptr;
    }
    if (connection->core != nullptr)
    {
        spa_hook_remove(&connection->coreListener);
        pipewire->api.coreDisconnect(connection->core);
        connection->core = nullptr;
    }
}

// The daemon went away: every stream's pw_stream and every device with
// it. Runs after the loop iteration that reported it.
static void LoseConnection(maudPipewire* pipewire)
{
    maudPipewireDropStreams(pipewire->context);
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        if (pipewire->nodes[i].used)
        {
            RemoveNode(pipewire, &pipewire->nodes[i]);
        }
    }
    maudPipewireDropCards(pipewire);
    DropCore(pipewire);
    pipewire->connection.nextAttempt = maudPipewireNow() + MAUD_PIPEWIRE_RETRY_NS;
}

static void OnCoreDone(void* data, uint32_t id, int seq)
{
    maudPipewire* pipewire = data;
    if (id == PW_ID_CORE && seq == pipewire->connection.pendingSync)
    {
        pipewire->connection.synced = true;
    }
}

static void OnCoreError(void* data, uint32_t id, int seq, int res, const char* message)
{
    (void)seq;
    (void)message;
    maudPipewire* pipewire = data;
    // Nothing of the core may be destroyed inside its own event; the
    // pump does it once the iteration returns.
    if (id == PW_ID_CORE && res == -EPIPE)
    {
        pipewire->connection.lost = true;
    }
}

static const struct pw_core_events s_coreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = OnCoreDone,
    .error = OnCoreError,
};

int64_t maudPipewireNow(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
}

bool maudPipewireRoundtrip(maudPipewire* pipewire, int64_t deadline)
{
    maudPipewireConnection* connection = &pipewire->connection;
    connection->synced = false;
    connection->pendingSync = pw_core_sync(connection->core, PW_ID_CORE, 0);
    pw_loop_enter(connection->loop);
    while (!connection->synced && !connection->lost)
    {
        int64_t remaining = deadline - maudPipewireNow();
        if (remaining <= 0)
        {
            break;
        }
        pw_loop_iterate(connection->loop, (int)(remaining / 1000000 + 1));
    }
    pw_loop_leave(connection->loop);
    return connection->synced && !connection->lost;
}

// Releases the connection's parts in the reverse order of creation.
static void Disconnect(maudPipewire* pipewire)
{
    maudPipewireConnection* connection = &pipewire->connection;
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        maudPipewireNode* node = &pipewire->nodes[i];
        if (node->used && node->proxy != nullptr)
        {
            spa_hook_remove(&node->listener);
            pipewire->api.proxyDestroy(node->proxy);
        }
    }
    maudPipewireDropCards(pipewire);
    DropCore(pipewire);
    if (connection->context != nullptr)
    {
        pipewire->api.contextDestroy(connection->context);
    }
    if (connection->loop != nullptr)
    {
        pipewire->api.loopDestroy(connection->loop);
    }
}

static void Release(maudContext* context, maudPipewire* pipewire)
{
    bool initialized = pipewire->api.library != nullptr;
    Disconnect(pipewire);
    if (initialized)
    {
        pipewire->api.deinit();
    }
    maudUnloadPipewire(&pipewire->api);
    maudContextRelease(context, pipewire, pipewire->bytes, alignof(maudPipewire));
    context->native = nullptr;
}

// Connects a core to the daemon and asks for the registry. False when
// the daemon does not answer.
static bool ConnectCore(maudPipewire* pipewire)
{
    maudPipewireConnection* connection = &pipewire->connection;
    connection->core = pipewire->api.contextConnect(connection->context, nullptr, 0);
    if (connection->core == nullptr)
    {
        return false;
    }
    pw_core_add_listener(connection->core, &connection->coreListener, &s_coreEvents, pipewire);
    connection->registry = pw_core_get_registry(connection->core, PW_VERSION_REGISTRY, 0);
    if (connection->registry == nullptr)
    {
        return false;
    }
    pw_registry_add_listener(connection->registry, &connection->registryListener, &s_registryEvents,
                             pipewire);
    return true;
}

// Sets up the loop and the context, which starts libpipewire's data
// thread, and connects the first core.
static bool Connect(maudPipewire* pipewire)
{
    maudPipewireConnection* connection = &pipewire->connection;
    pipewire->api.init(nullptr, nullptr);
    connection->loop = pipewire->api.loopNew(nullptr);
    connection->context = connection->loop != nullptr
                              ? pipewire->api.contextNew(connection->loop, nullptr, 0)
                              : nullptr;
    return connection->context != nullptr && ConnectCore(pipewire);
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t capacity = context->def.limits.devices;
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudPipewire) +
                   (size_t)capacity * (sizeof(maudPipewireNode) + sizeof(maudPipewireCard)) +
                   (size_t)streams * sizeof(maudPipewireStream);
    maudPipewire* pipewire = maudContextAllocate(context, bytes, alignof(maudPipewire));
    if (pipewire == nullptr)
    {
        return maud_errorCapacity;
    }
    *pipewire = (maudPipewire){
        .context = context,
        .nodes = (maudPipewireNode*)(pipewire + 1),
        .clock = {.graphRate = MAUD_PIPEWIRE_FALLBACK_RATE},
        .nodeCapacity = capacity,
        .cardCapacity = capacity,
        .bytes = bytes,
    };
    pipewire->cards = (maudPipewireCard*)(pipewire->nodes + capacity);
    pipewire->streams = (maudPipewireStream*)(pipewire->cards + capacity);
    memset(pipewire->nodes, 0, (size_t)capacity * sizeof(maudPipewireNode));
    memset(pipewire->cards, 0, (size_t)capacity * sizeof(maudPipewireCard));
    memset(pipewire->streams, 0, (size_t)streams * sizeof(maudPipewireStream));
    context->native = pipewire;
    if (!maudLoadPipewire(&pipewire->api))
    {
        Release(context, pipewire);
        return maud_errorUnsupported;
    }
    // The first round trip lists the globals; the second answers the
    // binds the first one made: node formats and the default metadata.
    int64_t deadline = maudPipewireNow() + MAUD_PIPEWIRE_DEADLINE_NS;
    if (!Connect(pipewire) || !maudPipewireRoundtrip(pipewire, deadline) ||
        !maudPipewireRoundtrip(pipewire, deadline))
    {
        Release(context, pipewire);
        return maud_errorUnsupported;
    }
    return maud_success;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

// Tries a new core once the retry time has come. Its devices arrive
// through the registry as the loop runs on.
static void Reconnect(maudPipewire* pipewire)
{
    maudPipewireConnection* connection = &pipewire->connection;
    if (maudPipewireNow() < connection->nextAttempt)
    {
        return;
    }
    if (!ConnectCore(pipewire))
    {
        DropCore(pipewire);
        connection->nextAttempt = maudPipewireNow() + MAUD_PIPEWIRE_RETRY_NS;
        return;
    }
    connection->lost = false;
}

static void Pump(maudContext* context)
{
    maudPipewire* pipewire = context->native;
    maudPipewireConnection* connection = &pipewire->connection;
    if (connection->lost && connection->core == nullptr)
    {
        Reconnect(pipewire);
    }
    pw_loop_enter(connection->loop);
    for (int i = 0; i < MAUD_PIPEWIRE_PUMP_ITERATIONS && !connection->lost; ++i)
    {
        if (pw_loop_iterate(connection->loop, 0) <= 0)
        {
            break;
        }
    }
    pw_loop_leave(connection->loop);
    if (connection->lost && connection->core != nullptr)
    {
        LoseConnection(pipewire);
    }
    if (!connection->lost)
    {
        maudPipewireReconnectStreams(context);
    }
}

// PipeWire runs every stream through its graph: a native stream at the
// graph's rate, any other rate on request through PipeWire's converter,
// and a required rate only when it is the graph's. Its streams run on
// libpipewire's thread, so there is no pull mode.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)device;
    uint32_t graph = ((const maudPipewire*)context->native)->clock.graphRate;
    if (def->mode == maud_modePull ||
        (def->ratePolicy == maud_rateRequired && def->sampleRate != graph))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? graph : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

// The halves of a duplex stream share a node group (StreamProperties).
static bool SharesClock(const maudContext* context, const maudStreamSlot* output,
                        const maudStreamSlot* input)
{
    (void)context;
    (void)output;
    (void)input;
    return true;
}

static const maudBackend s_pipewire = {
    .kind = maud_backendPipewire,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = maudPipewireAttachStream,
    .detachStream = maudPipewireDetachStream,
    .setStreamActive = maudPipewireSetStreamActive,
    .retargetStream = maudPipewireRetargetStream,
    .sharesClock = SharesClock,
    .rendersOnCaller = false,
};

const maudBackend* maudGetPipewireBackend(void)
{
    return &s_pipewire;
}
