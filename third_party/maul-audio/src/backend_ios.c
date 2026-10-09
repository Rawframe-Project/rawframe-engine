// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's context and devices, on iOS 15 and later. iOS
// routes audio itself and lets an application choose no output: the
// context has one output and one input, the session's, keyed "default",
// which streams follow wherever iOS routes them. Their rate and channels
// are the session's when the context opens. Streams run on RemoteIO
// units (ios_stream.c); the session's category follows them and the
// host's focus requests (ios_session.m). The session's interruptions
// hold the streams (maud_suspendPolicy) and are focus states; its route
// changes give the default devices the forms the route leads to.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "focus.h"
#include "follow.h"
#include "ios_core.h"
#include "ios_session.h"
#include "ios_stream.h"
#include "layout.h"

#include <mach/mach_time.h>
#include <string.h>

// The default device of a direction, leading where the route does.
static maudDeviceSpec DefaultSpec(const maudIos* ios, maudDirection direction, maudDeviceForm form)
{
    bool output = direction == maud_directionOutput;
    const char* name = output ? "Default output" : "Default input";
    // The microphone, mono until a stream asks for more; the unit
    // converts.
    return (maudDeviceSpec){
        .info =
            {
                .direction = direction,
                .nativeLayout = output ? maudLayoutWithChannels(ios->channels) : maud_layoutMono,
                .nativeSampleRate = ios->rate,
                .minSampleRate = ios->rate,
                .maxSampleRate = ios->rate,
                .form = form,
                // An object stream renders through the system's spatial
                // mixer, which takes any number of objects.
                .spatializer = output ? maud_spatializerOn : maud_spatializerUnknown,
                .spatialObjects = output ? MAUD_MAX_STREAM_OBJECTS : 0,
            },
        .name = name,
        .nameLength = maudCutUtf8(name, ios->context->def.limits.deviceTextBytes),
        .key = "default",
        .keyLength = 7,
    };
}

// Brings the default pair in line with the session's route.
// Brings the devices in line with the session: the default pair, where
// the route leads, and beside it the session's available inputs, which a
// stream pins through the session's preferred input. iOS lets no output
// be chosen.
static maudResult Rescan(maudIos* ios)
{
    uint32_t limit = ios->context->def.limits.devices;
    maudDeviceForm output = maud_formUnknown;
    maudDeviceForm input = maud_formUnknown;
    maudIosSessionRoute(&output, &input);
    ios->specs[0] = DefaultSpec(ios, maud_directionOutput, output);
    uint32_t count = 1;
    if (limit > 1)
    {
        ios->specs[1] = DefaultSpec(ios, maud_directionInput, input);
        count = 2;
    }
    uint32_t ports = limit > count ? maudIosSessionInputs(ios->ports, limit - count) : 0;
    for (uint32_t i = 0; i < ports; ++i)
    {
        const maudIosPort* port = &ios->ports[i];
        maudDeviceSpec spec = DefaultSpec(ios, maud_directionInput, port->form);
        spec.name = port->name;
        spec.nameLength = maudCutUtf8(port->name, ios->context->def.limits.deviceTextBytes);
        spec.key = port->key;
        spec.keyLength = strlen(port->key);
        ios->specs[count++] = spec;
    }
    return maudSyncDevices(ios->context, ios->specs, count, nullptr);
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    uint32_t devices = context->def.limits.devices;
    size_t bytes = sizeof(maudIos) + (size_t)streams * sizeof(maudIosStream) +
                   (size_t)devices * (sizeof(maudDeviceSpec) + sizeof(maudIosPort));
    maudIos* ios = maudContextAllocate(context, bytes, alignof(maudIos));
    if (ios == nullptr)
    {
        return maud_errorCapacity;
    }
    *ios = (maudIos){.context = context, .bytes = bytes};
    ios->streams = (maudIosStream*)(ios + 1);
    ios->specs = (maudDeviceSpec*)(ios->streams + streams);
    ios->ports = (maudIosPort*)(ios->specs + devices);
    memset(ios->streams, 0, (size_t)streams * sizeof(maudIosStream));
    context->native = ios;
    mach_timebase_info_data_t timebase = {0};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0)
    {
        timebase = (mach_timebase_info_data_t){1, 1};
    }
    ios->timebaseNumer = timebase.numer;
    ios->timebaseDenom = timebase.denom;
    maudIosSessionFormat(&ios->rate, &ios->channels);
    atomic_init(&ios->signals.began, false);
    atomic_init(&ios->signals.interruption, 0);
    atomic_init(&ios->signals.routeChanged, false);
    ios->observer = maudIosSessionObserve(&ios->signals);
    maudResult result = ios->observer != nullptr ? Rescan(ios) : maud_errorPlatform;
    if (result != maud_success)
    {
        maudIosSessionUnobserve(ios->observer);
        maudContextRelease(context, ios, bytes, alignof(maudIos));
        context->native = nullptr;
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    maudIos* ios = context->native;
    maudIosSessionUnobserve(ios->observer);
    // No stream runs: the session is deactivated, letting others resume.
    ios->session.focus = maud_focusRelease;
    bool updated = maudIosSessionUpdate(ios, (maudIosUse){0});
    (void)updated;
    maudContextRelease(context, ios, ios->bytes, alignof(maudIos));
    context->native = nullptr;
}

// RemoteIO converts the rate: a native stream takes the session's, a
// required rate must be it, and a converted stream any. A stream's
// period defaults to 10 ms.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t native =
        device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate : 48000u;
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

// The focus the context holds when nothing interrupts it.
static maudFocus Settled(const maudIos* ios)
{
    return ios->session.focus == maud_focusRelease ? maud_focusNone : maud_focusHeld;
}

// An interruption began: iOS has deactivated the session and stopped the
// units; the streams wait until it ends. When it ends with the hint to
// resume they run again; without it they wait for the host
// (maudResumeContext, or a focus request).
static void Interrupt(maudContext* context, int interruption)
{
    maudIos* ios = context->native;
    if (interruption == MAUD_IOS_INTERRUPTION_BEGAN)
    {
        ios->session.active = false;
        maudHoldStreams(context, true);
        maudReportFocus(context, maud_focusPaused);
    }
    else if (interruption == MAUD_IOS_INTERRUPTION_RESUME && context->held)
    {
        maudHoldStreams(context, false);
        maudReportFocus(context, Settled(ios));
    }
}

static void Pump(maudContext* context)
{
    maudIos* ios = context->native;
    if (atomic_exchange_explicit(&ios->signals.routeChanged, false, memory_order_acq_rel))
    {
        maudResult result = Rescan(ios);
        (void)result;
    }
    // A beginning since the last drain first, then the last event if it
    // ended one.
    int interruption =
        atomic_exchange_explicit(&ios->signals.interruption, 0, memory_order_acq_rel);
    if (atomic_exchange_explicit(&ios->signals.began, false, memory_order_acq_rel))
    {
        Interrupt(context, MAUD_IOS_INTERRUPTION_BEGAN);
    }
    if (interruption != MAUD_IOS_INTERRUPTION_BEGAN && interruption != 0)
    {
        Interrupt(context, interruption);
    }
}

// The host lets streams an interruption held run again.
static void ResumeContext(maudContext* context)
{
    if (context->held)
    {
        maudHoldStreams(context, false);
        maudReportFocus(context, Settled(context->native));
    }
}

// Focus is the session's: asking sets whether it mixes with others and
// activates it, and lets streams an interruption held run again;
// releasing mixes again and deactivates it when no stream runs.
static maudResult RequestFocus(maudContext* context, maudFocusRequest request, maudDeviceRole role)
{
    (void)role;
    maudIos* ios = context->native;
    maudFocusRequest previous = ios->session.focus;
    ios->session.focus = request;
    if (!maudIosUpdateSession(context, false))
    {
        ios->session.focus = previous;
        return maud_errorPlatform;
    }
    if (request != maud_focusRelease)
    {
        maudHoldStreams(context, false);
    }
    maudReportFocus(context, Settled(ios));
    return maud_success;
}

static const maudBackend s_ios = {
    .rendersObjects = true,
    .kind = maud_backendCoreAudio,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = maudIosAttachStream,
    .detachStream = maudIosDetachStream,
    .setStreamActive = maudIosSetStreamActive,
    .resumeContext = ResumeContext,
    .requestFocus = RequestFocus,
};

const maudBackend* maudGetIosBackend(void)
{
    return &s_ios;
}
