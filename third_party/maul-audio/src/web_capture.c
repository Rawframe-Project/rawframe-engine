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
// read index, quanta that did not fit) or a posted chunk, and reports
// with the ring's write index or the chunk, and the context frame after
// the quantum.
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
        "      this.port.postMessage([Atomics.load(this.index, 0), currentFrame + frames]);",
        "      return true;",
        "    }",
        "    const chunk = new Float32Array(frames * channels);",
        "    for (let i = 0; i < frames; ++i) {",
        "      for (let c = 0; c < channels; ++c) { chunk[i * channels + c] = input[Math.min(c, input.length - 1)][i]; }",
        "    }",
        "    this.port.postMessage([chunk, currentFrame + frames], [chunk.buffer]);",
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
                                    float* chunk, int voice, const char* device, int loopback), {
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
    // How long ago the first of the frames was captured: the track's own
    // latency before the context took it, at the context frame where the
    // graph took it. The context's output timestamp says when a context
    // frame is heard, and the base and output latencies how long before
    // that the graph processed it; deliveries come in a batch after the
    // device's burst, so the context's current time would place the frame
    // only as near as a burst. Before the first timestamp, without the
    // method, in the context's first quarter second (its first stamps run
    // early), or with a timestamp 100 ms old or placing the capture in the
    // future: the frames as if the last were captured now.
    function deliver(frames, first) {
        if (frames <= 0) {
            return;
        }
        const rate = entry.context.sampleRate;
        const stamp = entry.context.getOutputTimestamp ? entry.context.getOutputTimestamp() : null;
        let ago = latency + frames / rate;
        let stamped = 0;
        const now = performance.now() / 1000;
        if (first !== undefined && stamp && stamp.contextTime >= 0.25 && stamp.performanceTime &&
            now - stamp.performanceTime / 1000 <= 0.1) {
            const processed = stamp.performanceTime / 1000 + first / rate - stamp.contextTime -
                              entry.context.baseLatency - (entry.context.outputLatency || 0);
            if (now - processed + latency >= 0) {
                ago = now - processed + latency;
                stamped = 1;
            }
        }
        _maudWebCapture(context, slot, frames, ago, stamped, entry.routeEpoch);
    }
    function take(event) {
        if (record.closed) {
            return;
        }
        if (!record.ring) {
            const chunk = event.data[0];
            const frames = Math.min(chunk.length / channels, capacity);
            HEAPF32.set(chunk.subarray(0, frames * channels), base);
            deliver(frames, event.data[1] - chunk.length / channels);
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
        // The report's write index was at its context frame.
        deliver(frames, event.data[1] - ((event.data[0] - read) | 0));
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
    // A loopback stream captures what the user picks in the browser's
    // picker (a tab, a window or the screen, with its audio where the
    // browser gives it): the video track is stopped, and the browser's
    // voice processing, which it would apply to the display's audio too,
    // is asked off.
    const request = loopback !== 0
        ? navigator.mediaDevices.getDisplayMedia({video: true, systemAudio: "include", selfBrowserSurface: "include",
                                                  audio: {echoCancellation: false, noiseSuppression: false,
                                                          autoGainControl: false}})
        : navigator.mediaDevices.getUserMedia(constraints);
    Promise.all([entry.captureWorklet, request]).then(function (results) {
        const stream = results[1];
        stream.getVideoTracks().forEach(function (track) { track.stop(); });
        // A display grant without audio (a window, or a browser that
        // shares no system audio) is a refusal: the stream keeps waiting.
        if (record.closed || stream.getAudioTracks().length === 0) {
            stream.getTracks().forEach(function (track) { track.stop(); });
            return;
        }
        record.stream = stream;
        // The browser ends the track when the user stops sharing, the
        // microphone goes, or its permission is withdrawn.
        stream.getAudioTracks()[0].addEventListener("ended", function () {
            if (!record.closed) {
                _maudWebEnded(context, slot);
            }
        });
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
    }, function () {});
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
                                         double latency, int stamped, int epoch);

void maudWebCapture(maudContext* context, int slotIndex, int frames, double latency, int stamped,
                    int epoch)
{
    maudWeb* web = context->native;
    maudStreamCore* core = &context->streams.slots[slotIndex].core;
    if (atomic_load_explicit(&core->state, memory_order_acquire) != maud_streamRunning)
    {
        // Frames dropped while stopped: frame 0's time moves.
        maudWebResetClock(&web->streams[slotIndex].clock);
        return;
    }
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    maudPushPeriod(&core->period, web->streams[slotIndex].chunk, (uint32_t)frames);
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    maudWebStream* stream = &web->streams[slotIndex];
    maudWebStampClock(&stream->clock, &stream->epoch, core, (int64_t)(latency * 1e9), stamped != 0,
                      epoch, false);
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
    maudWebResetClock(&((maudWeb*)context->native)->streams[slotIndex].clock);
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

// The browser ended a capture's track. A loopback stream waits for the
// permission the user withdrew, which only a new stream made in a
// gesture's handler can ask for again; a microphone's capture is opened
// again, on the default if its device went, and waits for the
// permission while it opens or if it was withdrawn.
EMSCRIPTEN_KEEPALIVE void maudWebEnded(maudContext* context, int slotIndex);

void maudWebEnded(maudContext* context, int slotIndex)
{
    maudWeb* web = context->native;
    maudStreamSlot* slot = &context->streams.slots[slotIndex];
    if (slot->core.def.direction == maud_directionLoopback)
    {
        maudAwaitPermission(context, slot, true);
        return;
    }
    maudWebStream* entry = &web->streams[slotIndex];
    maudWebCloseCapture(entry->node);
    entry->node = maudWebOpenCapture(context, slot, web->handle, entry->chunk);
}

void maudWebFollowDefaultInput(maudContext* context)
{
    maudWeb* web = context->native;
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        char device[MAUD_WEB_KEY_BYTES] = {0};
        if (!slot->live || slot->core.def.direction != maud_directionInput ||
            web->streams[i].chunk == nullptr)
        {
            continue;
        }
        maudWebCaptureDevice(context, slot, device, sizeof(device));
        if (device[0] == '\0' && maudWebCaptureOffDefault(web->handle, web->streams[i].node) != 0)
        {
            maudWebEnded(context, (int)i);
        }
    }
}

int maudWebOpenCapture(maudContext* context, maudStreamSlot* slot, int handle, float* chunk)
{
    int index = (int)(slot - context->streams.slots);
    maudAwaitPermission(context, slot, true);
    char device[MAUD_WEB_KEY_BYTES] = {0};
    bool loopback = slot->core.def.direction == maud_directionLoopback;
    if (!loopback)
    {
        maudWebCaptureDevice(context, slot, device, sizeof(device));
    }
    return maudWebOpenCaptureNode(handle, context, index, (int)slot->core.period.channelCount,
                                  (int)MAUD_WEB_CAPACITY, chunk, (int)slot->core.def.voice, device,
                                  loopback ? 1 : 0);
}
