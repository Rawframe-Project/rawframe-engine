// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web devices. The JavaScript side keeps the latest enumerateDevices
// list on the context's entry and marks it new; the drain copies it into
// specs and syncs the table. Inputs are listed always, outputs only
// where AudioContext.setSinkId can choose one. The browser's "default"
// and "communications" entries are the Default devices already, and
// entries without an id (before the microphone is granted) name nothing.

#include "web_devices.h"

#include "context.h"
#include "device.h"

#include <emscripten/em_js.h>
#include <string.h>

// clang-format off

EM_JS(void, maudWebListDevices, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    entry.devices = [];
    entry.devicesNew = false;
    entry.listDevices = function () {
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
            return;
        }
        navigator.mediaDevices.enumerateDevices().then(function (list) {
            const outputs = typeof AudioContext.prototype.setSinkId === "function";
            entry.devices = list.filter(function (device) {
                return device.deviceId !== "" && device.deviceId !== "default" &&
                       device.deviceId !== "communications" &&
                       (device.kind === "audioinput" || (outputs && device.kind === "audiooutput"));
            });
            entry.devicesNew = true;
        }, function () {});
    };
    if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
        navigator.mediaDevices.addEventListener("devicechange", entry.listDevices);
    }
    entry.listDevices();
});

EM_JS(void, maudWebRelistDevices, (int handle), {
    globalThis.maudWeb.contexts[handle].listDevices();
});

EM_JS(void, maudWebStopListing, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    if (navigator.mediaDevices && navigator.mediaDevices.removeEventListener) {
        navigator.mediaDevices.removeEventListener("devicechange", entry.listDevices);
    }
});

// The new list's length, or -1 when none arrived since the last call.
EM_JS(int, maudWebTakeDevices, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    if (!entry.devicesNew) {
        return -1;
    }
    entry.devicesNew = false;
    return entry.devices.length;
});

// Copies one listed device's id and label; returns its direction.
EM_JS(int, maudWebDeviceAt, (int handle, int index, char* key, int keyBytes, char* name,
                             int nameBytes), {
    const device = globalThis.maudWeb.contexts[handle].devices[index];
    stringToUTF8(device.deviceId, key, keyBytes);
    stringToUTF8(device.label, name, nameBytes);
    return device.kind === "audiooutput" ? 0 : 1;
});

EM_JS(void, maudWebSetSinkId, (int handle, const char* key), {
    const context = globalThis.maudWeb.contexts[handle].context;
    if (typeof context.setSinkId === "function") {
        context.setSinkId(UTF8ToString(key)).catch(function () {});
    }
});

EM_JS_DEPS(maudWebDevices, "$stringToUTF8,$UTF8ToString");

// clang-format on

maudResult maudWebSyncDevices(maudContext* context)
{
    maudWeb* web = context->native;
    int listed = maudWebTakeDevices(web->handle);
    if (listed < 0)
    {
        return maud_success;
    }
    uint32_t count = 0;
    uint32_t rate = web->rate;
    for (int i = 0; i < listed && count < web->endpointCapacity; ++i)
    {
        maudWebEndpoint* endpoint = &web->endpoints[count];
        maudDirection direction =
            (maudDirection)maudWebDeviceAt(web->handle, i, endpoint->key, sizeof(endpoint->key),
                                           endpoint->name, sizeof(endpoint->name));
        web->specs[count++] = (maudDeviceSpec){
            .info = {.direction = direction,
                     .nativeLayout =
                         direction == maud_directionOutput ? maud_layoutStereo : maud_layoutMono,
                     .nativeSampleRate = rate,
                     .minSampleRate = rate,
                     .maxSampleRate = rate},
            .name = endpoint->name,
            .nameLength = maudCutUtf8(endpoint->name, context->def.limits.deviceTextBytes),
            .key = endpoint->key,
            .keyLength = strlen(endpoint->key),
        };
    }
    return maudSyncDevices(context, web->specs, count, "default");
}

// The key of a device, or "" for Default or a device that is gone.
static void KeyOf(const maudContext* context, maudDeviceId device, char* out, size_t capacity)
{
    out[0] = '\0';
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot != nullptr && slot->key.length < capacity &&
        !(slot->key.length == 7 && memcmp(slot->key.bytes, "default", 7) == 0))
    {
        memcpy(out, slot->key.bytes, slot->key.length);
        out[slot->key.length] = '\0';
    }
}

bool maudWebSinkFree(const maudContext* context, maudDeviceId device)
{
    char wanted[MAUD_WEB_KEY_BYTES];
    char playing[MAUD_WEB_KEY_BYTES];
    KeyOf(context, device, wanted, sizeof(wanted));
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        const maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live && slot->core.def.direction == maud_directionOutput)
        {
            KeyOf(context, slot->core.binding.current, playing, sizeof(playing));
            if (strcmp(wanted, playing) != 0)
            {
                return false;
            }
        }
    }
    return true;
}

void maudWebSetSink(maudContext* context, const maudStreamSlot* slot)
{
    char key[MAUD_WEB_KEY_BYTES];
    KeyOf(context, slot->core.binding.current, key, sizeof(key));
    maudWebSetSinkId(((maudWeb*)context->native)->handle, key);
}

void maudWebCaptureDevice(const maudContext* context, const maudStreamSlot* slot, char* out,
                          size_t capacity)
{
    KeyOf(context, slot->core.binding.current, out, capacity);
}
