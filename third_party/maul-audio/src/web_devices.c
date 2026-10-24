// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web devices. The JavaScript side keeps the latest enumerateDevices
// list on the context's entry and marks it new; the drain copies it into
// specs and syncs the table. Inputs are listed always, outputs only
// where AudioContext.setSinkId can choose one. The browser's "default"
// and "communications" entries are the Default devices already, and
// entries without an id (before the microphone is granted) name nothing.
// A sink the browser refuses is queued the same way, and the drain
// suspends the streams pinned to it. Each direction's default group is
// kept from each list, the "default" entry's where the browser lists one
// and the first device's otherwise, and a change of it is marked: the
// Default stand-in then leads elsewhere, and an open track stays on its
// device when the system's default microphone moves.

#include "web_devices.h"

#include "context.h"
#include "device.h"
#include "follow.h"
#include "notify.h"

#include <emscripten/em_js.h>
#include <string.h>

// clang-format off

EM_JS(void, maudWebListDevices, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    entry.devices = [];
    entry.devicesNew = false;
    entry.refusedSinks = [];
    entry.defaultGroups = ["", ""];
    entry.defaultMoved = [false, false];
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
            ["audiooutput", "audioinput"].forEach(function (kind, direction) {
                const of = list.filter(function (device) {
                    return device.kind === kind && device.deviceId !== "";
                });
                const first = of.find(function (device) { return device.deviceId === "default"; }) || of[0];
                const group = first ? first.groupId : "";
                const known = entry.defaultGroups[direction];
                if (group !== "" && known !== "" && group !== known) {
                    entry.defaultMoved[direction] = true;
                    entry.routeEpoch++;
                }
                if (group !== "") {
                    entry.defaultGroups[direction] = group;
                }
            });
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

// The browser refuses a sink it cannot play on: the device left since it
// was listed, or the page may not choose outputs.
EM_JS(void, maudWebSetSinkId, (int handle, const char* key), {
    const entry = globalThis.maudWeb.contexts[handle];
    if (typeof entry.context.setSinkId === "function") {
        const sinkId = UTF8ToString(key);
        entry.routeEpoch++;
        entry.context.setSinkId(sinkId).catch(function () { entry.refusedSinks.push(sinkId); });
    }
});

// Copies the oldest refused sink's id; 0 when none is queued.
EM_JS(int, maudWebTakeRefusedSink, (int handle, char* key, int keyBytes), {
    const entry = globalThis.maudWeb.contexts[handle];
    if (entry.refusedSinks.length === 0) {
        return 0;
    }
    stringToUTF8(entry.refusedSinks.shift(), key, keyBytes);
    return 1;
});

// 1 once if a direction's default group changed since the last call.
EM_JS(int, maudWebTakeDefaultMoved, (int handle, int direction), {
    const entry = globalThis.maudWeb.contexts[handle];
    const moved = entry.defaultMoved[direction];
    entry.defaultMoved[direction] = false;
    return moved ? 1 : 0;
});

// 1 if a capture node's track is on another group than the default
// microphone's; 0 while its track has not arrived or either is unknown.
EM_JS(int, maudWebCaptureOffDefault, (int handle, int node), {
    const record = globalThis.maudWeb.nodes[node];
    const group = globalThis.maudWeb.contexts[handle].defaultGroups[1];
    if (!record || !record.stream || group === "") {
        return 0;
    }
    const track = record.stream.getAudioTracks()[0];
    const settings = track ? track.getSettings() : {};
    return settings.groupId && settings.groupId !== group ? 1 : 0;
});

EM_JS_DEPS(maudWebDevices, "$stringToUTF8,$UTF8ToString");

// clang-format on

maudResult maudWebSyncDevices(maudContext* context)
{
    maudWeb* web = context->native;
    char refused[MAUD_WEB_KEY_BYTES];
    while (maudWebTakeRefusedSink(web->handle, refused, sizeof(refused)) != 0)
    {
        maudDeviceId device =
            maudFindDeviceByKey(context, maud_directionOutput, refused, strlen(refused));
        if (device.index1 != 0)
        {
            maudLoseDevice(context, device);
        }
    }
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

bool maudWebDefaultMoved(maudContext* context, maudDirection direction)
{
    maudWeb* web = context->native;
    if (maudWebTakeDefaultMoved(web->handle, (int)direction) == 0)
    {
        return false;
    }
    maudDeviceId device = maudFindDeviceByKey(context, direction, "default", 7);
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot != nullptr)
    {
        maudPostNotification(context, &(maudNotification){
                                          .kind = maud_notifyRouteChanged,
                                          .direction = direction,
                                          .device = device,
                                          .form = slot->info.form,
                                      });
    }
    return true;
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
