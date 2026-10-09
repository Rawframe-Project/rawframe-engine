// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Device notifications. Every change the system reports raises the
// flag: the drain lists the endpoints and defaults again, so which
// change it was does not matter. A property change counts only for the
// device format, which the device table holds.

#include "wasapi_notify.h"

#include <string.h>

static const GUID s_iidUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID s_iidNotificationClient = {
    0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
// PKEY_AudioEngine_DeviceFormat's format identifier.
// PKEY_AudioEndpoint_FormFactor's, whose property identifier is 0.

static maudWasapiNotifier* NotifierOf(IMMNotificationClient* client)
{
    return (maudWasapiNotifier*)client;
}

static void Raise(IMMNotificationClient* client)
{
    atomic_store_explicit(&NotifierOf(client)->changed, true, memory_order_release);
}

static HRESULT STDMETHODCALLTYPE QueryInterface(IMMNotificationClient* client, REFIID iid,
                                                void** objectOut)
{
    if (memcmp(iid, &s_iidUnknown, sizeof(GUID)) == 0 ||
        memcmp(iid, &s_iidNotificationClient, sizeof(GUID)) == 0)
    {
        *objectOut = client;
        return S_OK;
    }
    *objectOut = nullptr;
    return E_NOINTERFACE;
}

// The owner keeps the object alive, so the count is fixed.
static ULONG STDMETHODCALLTYPE AddRef(IMMNotificationClient* client)
{
    (void)client;
    return 1;
}

static ULONG STDMETHODCALLTYPE Release(IMMNotificationClient* client)
{
    (void)client;
    return 1;
}

static HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(IMMNotificationClient* client, LPCWSTR id,
                                                      DWORD state)
{
    (void)id;
    (void)state;
    Raise(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDeviceAdded(IMMNotificationClient* client, LPCWSTR id)
{
    (void)id;
    Raise(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDeviceRemoved(IMMNotificationClient* client, LPCWSTR id)
{
    (void)id;
    Raise(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(IMMNotificationClient* client,
                                                        EDataFlow flow, ERole role, LPCWSTR id)
{
    (void)flow;
    (void)role;
    (void)id;
    Raise(client);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(IMMNotificationClient* client, LPCWSTR id,
                                                        const PROPERTYKEY key)
{
    (void)id;
    (void)key;
    // Any property: the device format, the form factor, and the spatial
    // format the user chose, whose key Windows does not document. The
    // drain posts only what the new scan changed.
    Raise(client);
    return S_OK;
}

static const IMMNotificationClientVtbl s_vtable = {
    .QueryInterface = QueryInterface,
    .AddRef = AddRef,
    .Release = Release,
    .OnDeviceStateChanged = OnDeviceStateChanged,
    .OnDeviceAdded = OnDeviceAdded,
    .OnDeviceRemoved = OnDeviceRemoved,
    .OnDefaultDeviceChanged = OnDefaultDeviceChanged,
    .OnPropertyValueChanged = OnPropertyValueChanged,
};

void maudInitWasapiNotifier(maudWasapiNotifier* notifier)
{
    notifier->client.lpVtbl = &s_vtable;
    atomic_init(&notifier->changed, false);
}

bool maudTakeWasapiChanges(maudWasapiNotifier* notifier)
{
    return atomic_exchange_explicit(&notifier->changed, false, memory_order_acq_rel);
}
