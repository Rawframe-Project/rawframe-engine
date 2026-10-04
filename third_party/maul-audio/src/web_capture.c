// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Capture on the web. getUserMedia asks for the microphone with the
// browser's voice processing as the stream asks, off by default; once the track arrives, its source
// plays into a capture worklet mixed to the stream's channels. The
// worklet writes each quantum into a SharedArrayBuffer ring on an
// isolated page, or posts it elsewhere, and reports; the main thread
// copies what waits into the stream's chunk and pushes it through the
// period adapter, so the callback runs on the main thread.

#include "web_capture.h"

#include "clock.h"
#include "context.h"
#include "follow.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "web_core.h"
#include "web_devices.h"
#include "xrun.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

// clang-format off

// Copies each quantum's frames, interleaved, into the ring (write index,
// read index, quanta that did not fit) or a posted chunk, and reports.
EM_JS(void, maudWebAddCaptureProcessor, (int handle), {
    const source = [
        "class MaudCapture extends AudioWorkletProcessor {",
        "  constructor(options) {",
        "    super();",
        "    const p = options.processorOptions;",
        "    this.channels = p.channels;",
        "    this.capacity = p.capacity;",
        "    this.index = p.ring ? new Int32Array(p.ring, 0, 3) : null;",
        "    this.data = p.ring ? new Float32Array(p.ring, 12) : null;",
        "  }",
        "  process(inputs) {",
        "    const input = inputs[0];",
        "    if (input.length === 0) { return true; }",
        "    const frames = input[0].length;",
        "    const channels = this.channels;",
        "    if (this.index) {",
        "      const write = Atomics.load(this.index, 0);",
        "      const room = this.capacity - ((write - Atomics.load(this.index, 1)) | 0);",
        "      const count = Math.min(frames, room);",
        "      for (let i = 0; i < count; ++i) {",
        "        const at = ((write + i) & (this.capacity - 1)) * channels;",
        "        for (let c = 0; c < channels; ++c) { this.data[at + c] = input[Math.min(c, input.length - 1)][i]; }",
        "      }",
        "      Atomics.store(this.index, 0, (write + count) | 0);",
        "      if (count < frames) { Atomics.add(this.index, 2, 1); }",
        "      this.port.postMessage(0);",
        "      return true;",
        "    }",
        "    const chunk = new Float32Array(frames * channels);",
        "    for (let i = 0; i < frames; ++i) {",
        "      for (let c = 0; c < channels; ++c) { chunk[i * channels + c] = input[Math.min(c, input.length - 1)][i]; }",
        "    }",
        "    this.port.postMessage(chunk, [chunk.buffer]);",
        "    return true;",
        "  }",
        "}",
        "registerProcessor('maud-capture', MaudCapture);",
    ].join("\n");
    const entry = globalThis.maudWeb.contexts[handle];
    entry.captureWorklet = entry.context.audioWorklet.addModule(
        URL.createObjectURL(new Blob([source], {type: "text/javascript"})));
});

// Opens the microphone for a stream with the voice processing it asks
// for (maudVoiceProcessing flags); the node record waits for the track,
// then feeds the stream through maudWebCapture and tells the module,
// with the parts the track's settings say are on, through
// maudWebGranted.
EM_JS(int, maudWebOpenCaptureNode, (int handle, void* context, int slot, int channels, int capacity,
                                    float* chunk, int voice, const char* device), {
    const web = globalThis.maudWeb;
    const entry = web.contexts[handle];
    const record = {node: null, source: null, stream: null, closed: false, ring: null, index: null, data: null,
                    lostSeen: 0};
    if (typeof SharedArrayBuffer !== "undefined" && globalThis.crossOriginIsolated) {
        record.ring = new SharedArrayBuffer(12 + capacity * channels * 4);
        record.index = new Int32Array(record.ring, 0, 3);
        record.data = new Float32Array(record.ring, 12);
    }
    web.nodes.push(record);
    const id = web.nodes.length - 1;
    const base = chunk >> 2;
    let latency = 0;
    function deliver(frames) {
        if (frames > 0) {
            _maudWebCapture(context, slot, frames, latency + frames / entry.context.sampleRate);
        }
    }
    function take(event) {
        if (record.closed) {
            return;
        }
        if (!record.ring) {
            const frames = Math.min(event.data.length / channels, capacity);
            HEAPF32.set(event.data.subarray(0, frames * channels), base);
            deliver(frames);
            return;
        }
        const read = Atomics.load(record.index, 1);
        const frames = Math.min((Atomics.load(record.index, 0) - read) | 0, capacity);
        for (let i = 0; i < frames; ++i) {
            const at = ((read + i) & (capacity - 1)) * channels;
            for (let c = 0; c < channels; ++c) {
                HEAPF32[base + i * channels + c] = record.data[at + c];
            }
        }
        Atomics.store(record.index, 1, (read + frames) | 0);
        const lost = Atomics.load(record.index, 2);
        if (lost > record.lostSeen) {
            _maudWebXrun(context, slot, lost - record.lostSeen);
            record.lostSeen = lost;
        }
        deliver(frames);
    }
    const constraints = {audio: {
        echoCancellation: (voice & 1) !== 0,
        noiseSuppression: (voice & 2) !== 0,
        autoGainControl: (voice & 4) !== 0,
    }};
    const deviceId = UTF8ToString(device);
    if (deviceId !== "") {
        constraints.audio.deviceId = {exact: deviceId};
    }
    Promise.all([entry.captureWorklet, navigator.mediaDevices.getUserMedia(constraints)]).then(function (results) {
        const stream = results[1];
        if (record.closed) {
            stream.getTracks().forEach(function (track) { track.stop(); });
            return;
        }
        record.stream = stream;
        const settings = stream.getAudioTracks()[0].getSettings();
        latency = settings.latency || 0;
        const active = (settings.echoCancellation === true ? 1 : 0) |
                       (settings.noiseSuppression === true ? 2 : 0) |
                       (settings.autoGainControl === true ? 4 : 0);
        record.source = entry.context.createMediaStreamSource(stream);
        record.node = new AudioWorkletNode(entry.context, "maud-capture", {
            numberOfInputs: 1,
            numberOfOutputs: 0,
            channelCount: channels,
            channelCountMode: "explicit",
            channelInterpretation: "speakers",
            processorOptions: {channels: channels, capacity: capacity, ring: record.ring},
        });
        record.node.port.onmessage = take;
        record.source.connect(record.node);
        _maudWebGranted(context, slot, active);
    }, function () {
        record.refused = true;
    });
    return id;
});

EM_JS(void, maudWebCloseCapture, (int node), {
    const record = globalThis.maudWeb.nodes[node];
    record.closed = true;
    if (record.node !== null) {
        record.node.port.onmessage = null;
        record.source.disconnect();
    }
    if (record.stream !== null) {
        record.stream.getTracks().forEach(function (track) { track.stop(); });
    }
    globalThis.maudWeb.nodes[node] = null;
});

EM_JS_DEPS(maudWebCaptureDeps, "$UTF8ToString");

// clang-format on

// Pushes frames captured on the main thread through the stream while it
// runs; latency is how long ago the first of them was captured, in
// seconds.
EMSCRIPTEN_KEEPALIVE void maudWebCapture(maudContext* context, int slotIndex, int frames,
                                         double latency);

void maudWebCapture(maudContext* context, int slotIndex, int frames, double latency)
{
    const maudWeb* web = context->native;
    maudStreamCore* core = &context->streams.slots[slotIndex].core;
    if (atomic_load_explicit(&core->state, memory_order_acquire) != maud_streamRunning)
    {
        return;
    }
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    maudPushPeriod(&core->period, web->streams[slotIndex].chunk, (uint32_t)frames);
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    maudStampInputClock(core, (int64_t)(latency * 1e9));
    atomic_fetch_add_explicit(&core->position, (uint64_t)frames, memory_order_release);
}

// The worklet played `count` quanta short (an output) or could not fit
// `count` captured quanta in the ring (an input).
EMSCRIPTEN_KEEPALIVE void maudWebXrun(maudContext* context, int slotIndex, int count);

void maudWebXrun(maudContext* context, int slotIndex, int count)
{
    maudStreamCore* core = &context->streams.slots[slotIndex].core;
    for (int i = 0; i < count; ++i)
    {
        maudCountXrun(core);
    }
}

// The browser granted the microphone to a stream's node, with the voice
// processing parts active.
EMSCRIPTEN_KEEPALIVE void maudWebGranted(maudContext* context, int slotIndex, int active);

void maudWebGranted(maudContext* context, int slotIndex, int active)
{
    maudStreamSlot* slot = &context->streams.slots[slotIndex];
    maudReportVoice(&slot->core, (maudVoiceProcessing)active);
    maudAwaitPermission(context, slot, false);
    // Granted, the page sees its devices' ids and labels.
    maudWebRelistDevices(((maudWeb*)context->native)->handle);
}

int maudWebOpenCapture(maudContext* context, maudStreamSlot* slot, int handle, float* chunk)
{
    int index = (int)(slot - context->streams.slots);
    maudAwaitPermission(context, slot, true);
    char device[MAUD_WEB_KEY_BYTES];
    maudWebCaptureDevice(context, slot, device, sizeof(device));
    return maudWebOpenCaptureNode(handle, context, index, (int)slot->core.period.channelCount,
                                  (int)MAUD_WEB_CAPACITY, chunk, (int)slot->core.def.voice, device);
}
