// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's context and devices. Every COM call runs on the
// context's apartment thread, in the multithreaded apartment, whatever
// apartment the host's thread is in. Devices are the active endpoints, their formats read from the
// property store without activating them; a notification raises a flag
// and the drain lists endpoints and defaults again.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "layout.h"
#include "wasapi_core.h"
#include "wasapi_stream.h"

#include <spatialaudioclient.h>
#include <string.h>

static const GUID s_clsidEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID s_iidEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID s_iidSpatialClient = {
    0xBBF8E066, 0xAAAA, 0x49BE, {0x9A, 0x4D, 0xFD, 0x2A, 0x85, 0x8E, 0xA2, 0x7F}};
static const PROPERTYKEY s_friendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
static const PROPERTYKEY s_deviceFormat = {
    {0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};
static const PROPERTYKEY s_formFactor = {
    {0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 0};

// Converts wide to UTF-8 in out; false when it does not fit.
static bool ToUtf8(const wchar_t* wide, char* out, int capacity)
{
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, capacity, nullptr, nullptr) > 0;
}

// The endpoint ID of a device in UTF-8; false when it has none or it
// does not fit.
static bool IdOf(IMMDevice* device, char* out)
{
    LPWSTR id = nullptr;
    bool ok = SUCCEEDED(IMMDevice_GetId(device, &id)) && ToUtf8(id, out, MAUD_WASAPI_KEY_BYTES);
    CoTaskMemFree(id);
    return ok;
}

// Reads the audio engine's device format into info, if the store has
// one: its rate, and the layout of its channel count. Each layout has a
// count of its own, so the speaker mask would name no other.
static void ReadFormat(IPropertyStore* store, maudDeviceInfo* info)
{
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &s_deviceFormat, &value)) && value.vt == VT_BLOB &&
        value.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
        const WAVEFORMATEX* format = (const WAVEFORMATEX*)value.blob.pBlobData;
        info->nativeSampleRate = format->nSamplesPerSec;
        info->minSampleRate = format->nSamplesPerSec;
        info->maxSampleRate = format->nSamplesPerSec;
        info->nativeLayout = maudLayoutWithChannels(format->nChannels);
    }
    PropVariantClear(&value);
}

// The form an endpoint's EndpointFormFactor names; unknown for a
// network device, an unknown form or a store without one.
static maudDeviceForm ReadForm(IPropertyStore* store)
{
    static const maudDeviceForm forms[] = {
        [Speakers] = maud_formSpeakers,
        [LineLevel] = maud_formLine,
        [Headphones] = maud_formHeadphones,
        [Microphone] = maud_formMicrophone,
        [Headset] = maud_formHeadset,
        [Handset] = maud_formHandset,
        [UnknownDigitalPassthrough] = maud_formDigital,
        [SPDIF] = maud_formDigital,
        [DigitalAudioDisplayDevice] = maud_formDigital,
        [UnknownFormFactor] = maud_formUnknown,
    };
    PROPVARIANT value;
    PropVariantInit(&value);
    maudDeviceForm form = maud_formUnknown;
    if (SUCCEEDED(IPropertyStore_GetValue(store, &s_formFactor, &value)) && value.vt == VT_UI4 &&
        value.ulVal < sizeof(forms) / sizeof(forms[0]))
    {
        form = forms[value.ulVal];
    }
    PropVariantClear(&value);
    return form;
}

// What Windows' spatial sound does on an output endpoint: none where it
// cannot make a spatial client there; off while the user has chosen no
// spatial format, which leaves no dynamic objects; on with the format's
// objects otherwise. Windows tracks no head.
static void ReadSpatializer(IMMDevice* device, maudDeviceInfo* info)
{
    ISpatialAudioClient* client = nullptr;
    if (FAILED(IMMDevice_Activate(device, &s_iidSpatialClient, CLSCTX_INPROC_SERVER, nullptr,
                                  (void**)&client)))
    {
        info->spatializer = maud_spatializerNone;
        return;
    }
    UINT32 objects = 0;
    if (FAILED(ISpatialAudioClient_GetMaxDynamicObjectCount(client, &objects)))
    {
        objects = 0;
    }
    ISpatialAudioClient_Release(client);
    info->spatializer = objects > 0 ? maud_spatializerOn : maud_spatializerOff;
    info->spatialObjects = objects;
}

// Describes one endpoint into endpoint and spec; false when it cannot.
static bool Describe(const maudContext* context, IMMDevice* device, maudDirection direction,
                     maudWasapiEndpoint* endpoint, maudDeviceSpec* spec)
{
    IPropertyStore* store = nullptr;
    if (!IdOf(device, endpoint->key) ||
        FAILED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store)))
    {
        return false;
    }
    *spec = (maudDeviceSpec){.info = {.direction = direction}};
    PROPVARIANT name;
    PropVariantInit(&name);
    bool named = SUCCEEDED(IPropertyStore_GetValue(store, &s_friendlyName, &name)) &&
                 name.vt == VT_LPWSTR &&
                 ToUtf8(name.pwszVal, endpoint->name, MAUD_WASAPI_NAME_BYTES);
    PropVariantClear(&name);
    if (!named)
    {
        memcpy(endpoint->name, endpoint->key, sizeof(endpoint->key));
    }
    ReadFormat(store, &spec->info);
    spec->info.form = ReadForm(store);
    IPropertyStore_Release(store);
    if (direction == maud_directionOutput)
    {
        ReadSpatializer(device, &spec->info);
    }
    spec->name = endpoint->name;
    spec->nameLength = maudCutUtf8(endpoint->name, context->def.limits.deviceTextBytes);
    spec->key = endpoint->key;
    spec->keyLength = strlen(endpoint->key);
    return true;
}

// Lists the active endpoints of one flow from count on; returns the new
// count.
static uint32_t ScanFlow(maudContext* context, EDataFlow flow, uint32_t count)
{
    maudWasapi* wasapi = context->native;
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(wasapi->enumerator, flow, DEVICE_STATE_ACTIVE,
                                                      &collection)))
    {
        return count;
    }
    UINT total = 0;
    IMMDeviceCollection_GetCount(collection, &total);
    maudDirection direction = flow == eRender ? maud_directionOutput : maud_directionInput;
    for (UINT i = 0; i < total && count < context->def.limits.devices; ++i)
    {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(IMMDeviceCollection_Item(collection, i, &device)))
        {
            count += Describe(context, device, direction, &wasapi->endpoints[count],
                              &wasapi->specs[count])
                         ? 1u
                         : 0u;
            IMMDevice_Release(device);
        }
    }
    IMMDeviceCollection_Release(collection);
    return count;
}

// Points one role's default of one flow at the system's.
static void ReadDefault(maudContext* context, EDataFlow flow, ERole role, maudDeviceRole ours)
{
    maudWasapi* wasapi = context->native;
    IMMDevice* device = nullptr;
    char key[MAUD_WASAPI_KEY_BYTES];
    if (FAILED(
            IMMDeviceEnumerator_GetDefaultAudioEndpoint(wasapi->enumerator, flow, role, &device)))
    {
        return;
    }
    bool found = IdOf(device, key);
    IMMDevice_Release(device);
    maudDeviceId id = maudFindDeviceByKey(
        context, flow == eRender ? maud_directionOutput : maud_directionInput, key, strlen(key));
    if (found && id.index1 != 0)
    {
        maudSetDefaultDevice(context, ours, id);
    }
}

// Lists the endpoints again, syncs the table, and reads the defaults.
static maudResult Rescan(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    uint32_t count = ScanFlow(context, eCapture, ScanFlow(context, eRender, 0));
    maudResult result = maudSyncDevices(context, wasapi->specs, count, nullptr);
    for (int flow = 0; flow < 2; ++flow)
    {
        ReadDefault(context, flow == 0 ? eRender : eCapture, eConsole, maud_roleGeneral);
        ReadDefault(context, flow == 0 ? eRender : eCapture, eCommunications,
                    maud_roleCommunications);
    }
    return result;
}

// The context's COM objects, released on the apartment's thread.
static void ReleaseObjects(void* user)
{
    maudWasapi* wasapi = user;
    if (wasapi->registered)
    {
        IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(wasapi->enumerator,
                                                                   &wasapi->notifier.client);
    }
    if (wasapi->enumerator != nullptr)
    {
        IMMDeviceEnumerator_Release(wasapi->enumerator);
    }
}

static void Release(maudContext* context, maudWasapi* wasapi)
{
    if (wasapi->apartment.joined)
    {
        maudCallInWasapiApartment(&wasapi->apartment, ReleaseObjects, wasapi);
    }
    maudStopWasapiApartment(&wasapi->apartment);
    maudContextRelease(context, wasapi, wasapi->bytes, alignof(maudWasapi));
    context->native = nullptr;
}

// A context call run on the apartment's thread: its arguments and
// result.
typedef struct Call
{
    maudContext* context;
    maudStreamSlot* slot;
    bool active;
    maudResult result;
} Call;

// The enumerator, its notifications, and the first scan.
static void Open(void* user)
{
    Call* call = user;
    maudWasapi* wasapi = call->context->native;
    if (FAILED(CoCreateInstance(&s_clsidEnumerator, nullptr, CLSCTX_ALL, &s_iidEnumerator,
                                (void**)&wasapi->enumerator)))
    {
        call->result = maud_errorUnsupported;
        return;
    }
    wasapi->registered = SUCCEEDED(IMMDeviceEnumerator_RegisterEndpointNotificationCallback(
        wasapi->enumerator, &wasapi->notifier.client));
    call->result = Rescan(call->context);
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t devices = context->def.limits.devices;
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudWasapi) +
                   (size_t)devices * (sizeof(maudWasapiEndpoint) + sizeof(maudDeviceSpec)) +
                   (size_t)streams * sizeof(maudWasapiStream);
    maudWasapi* wasapi = maudContextAllocate(context, bytes, alignof(maudWasapi));
    if (wasapi == nullptr)
    {
        return maud_errorCapacity;
    }
    *wasapi = (maudWasapi){.context = context, .bytes = bytes};
    wasapi->endpoints = (maudWasapiEndpoint*)(wasapi + 1);
    wasapi->specs = (maudDeviceSpec*)(wasapi->endpoints + devices);
    wasapi->streams = (maudWasapiStream*)(wasapi->specs + devices);
    memset(wasapi->streams, 0, (size_t)streams * sizeof(maudWasapiStream));
    maudInitWasapiNotifier(&wasapi->notifier);
    context->native = wasapi;
    if (!maudStartWasapiApartment(&wasapi->apartment))
    {
        Release(context, wasapi);
        return maud_errorPlatform;
    }
    Call call = {.context = context, .result = maud_success};
    maudCallInWasapiApartment(&wasapi->apartment, Open, &call);
    if (call.result != maud_success)
    {
        Release(context, wasapi);
    }
    return call.result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

static void PumpCall(void* user)
{
    Call* call = user;
    maudWasapi* wasapi = call->context->native;
    if (maudTakeWasapiChanges(&wasapi->notifier))
    {
        maudResult result = Rescan(call->context);
        (void)result;
    }
    maudWasapiResumeStreams(call->context);
}

// The drain crosses to the apartment's thread only when there is work:
// a change reported, or a running stream to open again.
static void Pump(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    if (atomic_load_explicit(&wasapi->notifier.changed, memory_order_acquire) ||
        maudWasapiStreamsToResume(context))
    {
        Call call = {.context = context};
        maudCallInWasapiApartment(&wasapi->apartment, PumpCall, &call);
    }
}

static void AttachCall(void* user)
{
    Call* call = user;
    call->result = maudWasapiAttachStream(call->context, call->slot);
}

static maudResult AttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWasapi* wasapi = context->native;
    Call call = {.context = context, .slot = slot};
    maudCallInWasapiApartment(&wasapi->apartment, AttachCall, &call);
    return call.result;
}

static void DetachCall(void* user)
{
    Call* call = user;
    maudWasapiDetachStream(call->context, call->slot);
}

static void DetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWasapi* wasapi = context->native;
    Call call = {.context = context, .slot = slot};
    maudCallInWasapiApartment(&wasapi->apartment, DetachCall, &call);
}

static void ActiveCall(void* user)
{
    Call* call = user;
    maudWasapiSetStreamActive(call->context, call->slot, call->active);
}

static void SetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudWasapi* wasapi = context->native;
    Call call = {.context = context, .slot = slot, .active = active};
    maudCallInWasapiApartment(&wasapi->apartment, ActiveCall, &call);
}

static void RetargetCall(void* user)
{
    Call* call = user;
    maudWasapiRetargetStream(call->context, call->slot);
}

static void RetargetStream(maudContext* context, maudStreamSlot* slot)
{
    maudWasapi* wasapi = context->native;
    Call call = {.context = context, .slot = slot};
    maudCallInWasapiApartment(&wasapi->apartment, RetargetCall, &call);
}

// Shared mode runs at the engine's rate: a native stream takes it, a
// required rate must be it, and only a platform-converted stream may
// differ. With no device yet, the engine's rate is taken as 48 kHz.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t engine =
        device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate : 48000;
    if (def->mode == maud_modePull ||
        (def->ratePolicy == maud_rateRequired && def->sampleRate != engine))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? engine : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

// A mono or stereo shared stream goes "direct to ears", unvirtualized;
// more channels may go through a spatial format the user chose.
static maudSpatialMark MarkStream(const maudStreamDef* def, const maudStreamFormat* format)
{
    (void)def;
    return maudGetLayoutChannelCount(format->layout) <= 2 ? maud_markHonored : maud_markUnknown;
}

static const maudBackend s_wasapi = {
    .rendersObjects = true,
    .markStream = MarkStream,
    .kind = maud_backendWasapi,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .exclusive = true,
    .attachStream = AttachStream,
    .detachStream = DetachStream,
    .setStreamActive = SetStreamActive,
    .retargetStream = RetargetStream,
    .reopensOnMove = true,
    .rendersOnCaller = false,
};

const maudBackend* maudGetWasapiBackend(void)
{
    return &s_wasapi;
}
