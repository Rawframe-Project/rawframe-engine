// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's context and devices, on Android 11 (API 30) and
// later. AAudio lists no devices and reports no changes to them, and
// Android has no query for the device media goes to: its policy routes
// a stream opened without one. So the context always has a default
// output and input, keyed "default", which streams on the null id follow
// wherever Android moves them. The output's rate and channels are those
// of an AAudio stream opened on it once, at the start; the input is
// described from the output, since opening one would show the
// microphone in use. Given a Java VM and an Android Context, the Java
// half (aaudio_java.c) lists every user-facing device beside the
// defaults, for streams that pin one, and raises a flag when they
// change, on which the drain lists them again.

#include "aaudio_core.h"
#include "aaudio_java.h"
#include "aaudio_stream.h"
#include "backend.h"
#include "context.h"
#include "device.h"
#include "focus.h"
#include "layout.h"

#include <android/api-level.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// The rate a device is taken to run at when the probe cannot tell.
#define FALLBACK_RATE 48000u

// The default output's rate and channel count, from a stream opened on
// it and closed; false when AAudio cannot open one.
static bool Probe(uint32_t* rate, uint32_t* channels)
{
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
    {
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStream* stream = nullptr;
    aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK)
    {
        return false;
    }
    int32_t probedRate = AAudioStream_getSampleRate(stream);
    int32_t probedChannels = AAudioStream_getChannelCount(stream);
    aaudio_result_t closed = AAudioStream_close(stream);
    (void)closed;
    *rate = probedRate > 0 ? (uint32_t)probedRate : FALLBACK_RATE;
    *channels = probedChannels > 0 ? (uint32_t)probedChannels : 2u;
    return true;
}

// What an AudioDeviceInfo type is: its form, and the name a built-in
// device takes (others take their product name). Types not here are
// internal endpoints (telephony, tuners, the remote submix, buses, the
// echo reference) and are not listed.
typedef struct Kind
{
    int32_t type;
    maudDeviceForm form;
    const char* builtIn;
} Kind;

static const Kind s_kinds[] = {
    {1, maud_formHandset, "Earpiece"},       // TYPE_BUILTIN_EARPIECE
    {2, maud_formSpeakers, "Speaker"},       // TYPE_BUILTIN_SPEAKER
    {3, maud_formHeadset, nullptr},          // TYPE_WIRED_HEADSET
    {4, maud_formHeadphones, nullptr},       // TYPE_WIRED_HEADPHONES
    {5, maud_formLine, nullptr},             // TYPE_LINE_ANALOG
    {6, maud_formDigital, nullptr},          // TYPE_LINE_DIGITAL
    {7, maud_formHeadset, nullptr},          // TYPE_BLUETOOTH_SCO
    {8, maud_formHeadphones, nullptr},       // TYPE_BLUETOOTH_A2DP
    {9, maud_formDigital, nullptr},          // TYPE_HDMI
    {10, maud_formDigital, nullptr},         // TYPE_HDMI_ARC
    {11, maud_formUnknown, nullptr},         // TYPE_USB_DEVICE
    {12, maud_formUnknown, nullptr},         // TYPE_USB_ACCESSORY
    {13, maud_formUnknown, nullptr},         // TYPE_DOCK
    {15, maud_formMicrophone, "Microphone"}, // TYPE_BUILTIN_MIC
    {19, maud_formLine, nullptr},            // TYPE_AUX_LINE
    {22, maud_formHeadset, nullptr},         // TYPE_USB_HEADSET
    {23, maud_formHeadphones, nullptr},      // TYPE_HEARING_AID
    {26, maud_formHeadset, nullptr},         // TYPE_BLE_HEADSET
    {27, maud_formSpeakers, nullptr},        // TYPE_BLE_SPEAKER
    {29, maud_formDigital, nullptr},         // TYPE_HDMI_EARC
};

static const Kind* KindOf(int32_t type)
{
    for (size_t i = 0; i < sizeof(s_kinds) / sizeof(s_kinds[0]); ++i)
    {
        if (s_kinds[i].type == type)
        {
            return &s_kinds[i];
        }
    }
    return nullptr;
}

// Adds a device to the scan; false when the context's device limit is
// reached.
static bool Add(maudAaudio* aaudio, maudDirection direction, int32_t id, maudDeviceInfo info)
{
    maudContext* context = aaudio->context;
    if (aaudio->endpointCount >= context->def.limits.devices)
    {
        return false;
    }
    maudAaudioEndpoint* endpoint = &aaudio->endpoints[aaudio->endpointCount];
    info.direction = direction;
    endpoint->direction = direction;
    endpoint->id = id;
    aaudio->specs[aaudio->endpointCount] = (maudDeviceSpec){
        .info = info,
        .name = endpoint->name,
        .nameLength = maudCutUtf8(endpoint->name, context->def.limits.deviceTextBytes),
        .key = endpoint->key,
        .keyLength = strlen(endpoint->key),
    };
    aaudio->endpointCount++;
    return true;
}

// Android's Spatializer works on the current route, which the default
// output follows: its state, from Java where the context has it. Before
// API 32 there is none; without Java a newer Android does not say.
// Devices a stream pins say nothing, as Android tells their state only to
// the system.
static void DescribeSpatializer(maudAaudio* aaudio, maudDeviceInfo* info)
{
    int32_t state = android_get_device_api_level() < 32 ? 0 : maudAaudioSpatializerJava(aaudio);
    if (state < 0)
    {
        return;
    }
    bool available = (state & 2) != 0;
    bool enabled = (state & 4) != 0;
    info->spatializer = state == 0 || !available ? maud_spatializerNone
                        : enabled                ? maud_spatializerOn
                                                 : maud_spatializerOff;
    info->headTracking = (state & 8) != 0;
}

static void AddDefault(maudAaudio* aaudio, maudDirection direction)
{
    bool output = direction == maud_directionOutput;
    uint32_t count = aaudio->endpointCount;
    if (count >= aaudio->context->def.limits.devices)
    {
        return;
    }
    maudAaudioEndpoint* endpoint = &aaudio->endpoints[count];
    snprintf(endpoint->key, sizeof(endpoint->key), "default");
    snprintf(endpoint->name, sizeof(endpoint->name), "%s",
             output ? "Default output" : "Default input");
    // The microphone, mono until a stream asks for more; AAudio converts.
    maudDeviceInfo info = {
        .nativeLayout = output ? maudLayoutWithChannels(aaudio->channels) : maud_layoutMono,
        .nativeSampleRate = aaudio->rate,
        .minSampleRate = aaudio->rate,
        .maxSampleRate = aaudio->rate,
    };
    if (output)
    {
        DescribeSpatializer(aaudio, &info);
    }
    bool added = Add(aaudio, direction, 0, info);
    (void)added;
}

// Adds one device Java listed, keyed by its type and address (its
// product name when it has none).
static void Listed(maudAaudio* aaudio, maudDirection direction, const maudAaudioListing* listing)
{
    const Kind* kind = KindOf(listing->type);
    uint32_t count = aaudio->endpointCount;
    if (kind == nullptr || count >= aaudio->context->def.limits.devices)
    {
        return;
    }
    maudAaudioEndpoint* endpoint = &aaudio->endpoints[count];
    const char* where = listing->address[0] != '\0' ? listing->address : listing->product;
    snprintf(endpoint->key, sizeof(endpoint->key), "%d:%s", (int)listing->type, where);
    const char* name = kind->builtIn != nullptr ? kind->builtIn : listing->product;
    snprintf(endpoint->name, sizeof(endpoint->name), "%s", name[0] != '\0' ? name : "Device");
    uint32_t low = listing->lowRate > 0 ? (uint32_t)listing->lowRate : aaudio->rate;
    uint32_t high = listing->highRate > 0 ? (uint32_t)listing->highRate : aaudio->rate;
    uint32_t native = aaudio->rate < low ? low : aaudio->rate > high ? high : aaudio->rate;
    uint32_t channels = listing->channels > 0               ? (uint32_t)listing->channels
                        : direction == maud_directionOutput ? aaudio->channels
                                                            : 1u;
    bool added = Add(aaudio, direction, listing->id,
                     (maudDeviceInfo){
                         .nativeLayout = maudLayoutWithChannels(channels),
                         .nativeSampleRate = native,
                         .minSampleRate = low,
                         .maxSampleRate = high,
                         .form = kind->form,
                     });
    (void)added;
}

// Points both roles of a direction at its default device.
static void PointDefaults(maudContext* context, maudDirection direction)
{
    maudDeviceId id = maudFindDeviceByKey(context, direction, "default", 7);
    maudSetDefaultDevice(context, maud_roleGeneral, id);
    maudSetDefaultDevice(context, maud_roleCommunications, id);
}

static maudResult Rescan(maudAaudio* aaudio)
{
    aaudio->endpointCount = 0;
    AddDefault(aaudio, maud_directionOutput);
    AddDefault(aaudio, maud_directionInput);
    if (aaudio->hasJava)
    {
        maudAaudioListJava(aaudio, maud_directionOutput, Listed);
        maudAaudioListJava(aaudio, maud_directionInput, Listed);
    }
    maudResult result =
        maudSyncDevices(aaudio->context, aaudio->specs, aaudio->endpointCount, nullptr);
    PointDefaults(aaudio->context, maud_directionOutput);
    PointDefaults(aaudio->context, maud_directionInput);
    return result;
}

// Loads the calls past API 30 the backend uses, where the device has
// them.
static void LoadLate(maudAaudioLate* late)
{
    *late = (maudAaudioLate){0};
    if (android_get_device_api_level() < 32)
    {
        return;
    }
    late->library = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (late->library == nullptr)
    {
        return;
    }
    late->setContentSpatialized = (void (*)(AAudioStreamBuilder*, bool))dlsym(
        late->library, "AAudioStreamBuilder_setIsContentSpatialized");
    late->setSpatializationBehavior =
        (void (*)(AAudioStreamBuilder*, aaudio_spatialization_behavior_t))dlsym(
            late->library, "AAudioStreamBuilder_setSpatializationBehavior");
    late->isContentSpatialized =
        (bool (*)(AAudioStream*))dlsym(late->library, "AAudioStream_isContentSpatialized");
}

static void Release(maudContext* context, maudAaudio* aaudio)
{
    if (aaudio->late.library != nullptr)
    {
        int closed = dlclose(aaudio->late.library);
        (void)closed;
    }
    if (aaudio->hasJava)
    {
        maudAaudioCloseJava(aaudio);
    }
    maudContextRelease(context, aaudio, aaudio->bytes, alignof(maudAaudio));
    context->native = nullptr;
}

// Carves the context's block: the state, then the streams, the specs
// and the endpoints.
static maudAaudio* Allocate(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    uint32_t devices = context->def.limits.devices;
    size_t bytes = sizeof(maudAaudio) + (size_t)streams * sizeof(maudAaudioStream) +
                   (size_t)devices * (sizeof(maudDeviceSpec) + sizeof(maudAaudioEndpoint));
    maudAaudio* aaudio = maudContextAllocate(context, bytes, alignof(maudAaudio));
    if (aaudio == nullptr)
    {
        return nullptr;
    }
    *aaudio = (maudAaudio){.context = context, .bytes = bytes};
    aaudio->streams = (maudAaudioStream*)(aaudio + 1);
    aaudio->specs = (maudDeviceSpec*)(aaudio->streams + streams);
    aaudio->endpoints = (maudAaudioEndpoint*)(aaudio->specs + devices);
    memset(aaudio->streams, 0, (size_t)streams * sizeof(maudAaudioStream));
    atomic_init(&aaudio->signals.changed, false);
    atomic_init(&aaudio->signals.focus, 0);
    return aaudio;
}

static maudResult OpenContext(maudContext* context)
{
    maudAaudio* aaudio = Allocate(context);
    if (aaudio == nullptr)
    {
        return maud_errorCapacity;
    }
    context->native = aaudio;
    LoadLate(&aaudio->late);
    aaudio->rate = FALLBACK_RATE;
    aaudio->channels = 2;
    maudResult result =
        Probe(&aaudio->rate, &aaudio->channels) ? maud_success : maud_errorUnsupported;
    // Handles that do not lead to the library's class are the platform
    // failing the host: the class is not compiled in.
    if (result == maud_success && context->def.androidJavaVm != nullptr &&
        !maudAaudioOpenJava(aaudio, context->def.androidJavaVm, context->def.androidContext))
    {
        result = maud_errorPlatform;
    }
    if (result == maud_success)
    {
        result = Rescan(aaudio);
    }
    if (result != maud_success)
    {
        Release(context, aaudio);
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

// AudioManager's focus changes, as states.
#define FOCUS_GAIN                    1
#define FOCUS_LOSS                    (-1)
#define FOCUS_LOSS_TRANSIENT          (-2)
#define FOCUS_LOSS_TRANSIENT_CAN_DUCK (-3)

static void Pump(maudContext* context)
{
    maudAaudio* aaudio = context->native;
    if (atomic_exchange_explicit(&aaudio->signals.changed, false, memory_order_acq_rel))
    {
        maudResult result = Rescan(aaudio);
        (void)result;
    }
    int change = atomic_exchange_explicit(&aaudio->signals.focus, 0, memory_order_acq_rel);
    if (change == FOCUS_GAIN)
    {
        maudReportFocus(context, maud_focusHeld);
    }
    else if (change == FOCUS_LOSS)
    {
        maudReportFocus(context, maud_focusLost);
    }
    else if (change == FOCUS_LOSS_TRANSIENT)
    {
        maudReportFocus(context, maud_focusPaused);
    }
    else if (change == FOCUS_LOSS_TRANSIENT_CAN_DUCK)
    {
        maudReportFocus(context, maud_focusDucked);
    }
    maudAaudioResumeStreams(context);
}

// AudioManager's request results.
#define FOCUS_REFUSED 0
#define FOCUS_GRANTED 1

// A request granted holds focus at once; one delayed holds it when the
// gain arrives; a release holds none. A focus change the listener stored
// before the request is stale and dropped.
static maudResult RequestFocus(maudContext* context, maudFocusRequest request, maudDeviceRole role)
{
    maudAaudio* aaudio = context->native;
    if (!aaudio->hasJava)
    {
        return maud_errorUnsupported;
    }
    int32_t result = maudAaudioRequestFocusJava(aaudio, request, role == maud_roleCommunications);
    atomic_store_explicit(&aaudio->signals.focus, 0, memory_order_release);
    if (result == FOCUS_REFUSED)
    {
        return maud_errorPlatform;
    }
    if (request == maud_focusRelease)
    {
        maudReportFocus(context, maud_focusNone);
    }
    else if (result == FOCUS_GRANTED)
    {
        maudReportFocus(context, maud_focusHeld);
    }
    return maud_success;
}

// AAudio converts the rate in shared mode: a native stream takes the
// device's, a required rate must be it, and a converted stream any. A
// stream's period defaults to 10 ms.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t native = device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate
                                                                         : FALLBACK_RATE;
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

// Android spatializes output from API 32 (Android 12L): before, a marked
// stream is left as it is; after, the open stream says (aaudio_stream.c).
static maudSpatialMark MarkStream(const maudStreamDef* def, const maudStreamFormat* format)
{
    (void)def;
    (void)format;
    return android_get_device_api_level() < 32 ? maud_markHonored : maud_markUnknown;
}

static const maudBackend s_aaudio = {
    .markStream = MarkStream,
    .kind = maud_backendAaudio,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = maudAaudioAttachStream,
    .detachStream = maudAaudioDetachStream,
    .setStreamActive = maudAaudioSetStreamActive,
    .exclusive = true,
    .requestFocus = RequestFocus,
};

const maudBackend* maudGetAaudioBackend(void)
{
    return &s_aaudio;
}
