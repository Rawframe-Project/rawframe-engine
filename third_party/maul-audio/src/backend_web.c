// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend. The context owns an AudioContext, kept by the
// JavaScript side in a table under a handle. Each stream plays through
// an AudioWorkletProcessor that reports every quantum it plays; the
// main thread answers by rendering, in steps of 128 frames, up to a
// fill target. On a cross-origin isolated page the frames go through a
// SharedArrayBuffer ring, elsewhere as posted chunks. The target starts
// at the browser's baseLatency plus 256 frames and grows by 128 for
// each quantum the worklet plays short. The browser's autoplay policy
// holds a context until a user gesture; the drain reads the
// AudioContext's state and suspends or resumes the streams.

#include "backend.h"
#include "clock.h"
#include "context.h"
#include "device.h"
#include "follow.h"
#include "period.h"
#include "thread.h"
#include "web_capture.h"
#include "web_core.h"
#include "web_devices.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <string.h>

// clang-format off

// Makes an AudioContext and returns its handle, or 0 where the page has
// no Web Audio.
EM_JS(int, maudWebOpen, (void), {
    if (typeof AudioContext === "undefined") {
        return 0;
    }
    const web = globalThis.maudWeb || (globalThis.maudWeb = {contexts: [null], nodes: [null]});
    web.contexts.push({context: new AudioContext(), worklet: null});
    return web.contexts.length - 1;
});

EM_JS(int, maudWebRate, (int handle), {
    return globalThis.maudWeb.contexts[handle].context.sampleRate;
});

// 1 while the browser holds the context, 0 when it runs.
EM_JS(int, maudWebHeld, (int handle), {
    return globalThis.maudWeb.contexts[handle].context.state === "suspended" ? 1 : 0;
});

EM_JS(void, maudWebResume, (int handle), {
    globalThis.maudWeb.contexts[handle].context.resume();
});

// Suspends the AudioContext for the host, or resumes it.
EM_JS(void, maudWebSuspend, (int handle, int suspended), {
    const context = globalThis.maudWeb.contexts[handle].context;
    if (suspended) {
        context.suspend();
    } else {
        context.resume();
    }
});

EM_JS(void, maudWebClose, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    entry.context.close();
    globalThis.maudWeb.contexts[handle] = null;
});

// The processor plays interleaved frames from a SharedArrayBuffer ring
// (its write index, read index and short count in the first 12 bytes)
// or from posted chunks, counts the quanta it plays short, and posts
// after each quantum. Told to drop, it discards what it holds, counted
// as played so the main thread refills. The
// source is plain string literals: EM_JS passes its body through the C
// preprocessor, which would split arrow functions and template
// literals.
EM_JS(void, maudWebAddProcessor, (int handle), {
    const source = [
        "class MaudStream extends AudioWorkletProcessor {",
        "  constructor(options) {",
        "    super();",
        "    const p = options.processorOptions;",
        "    this.channels = p.channels;",
        "    this.played = 0;",
        "    this.short = 0;",
        "    this.index = p.ring ? new Int32Array(p.ring, 0, 3) : null;",
        "    this.data = p.ring ? new Float32Array(p.ring, 12) : null;",
        "    this.mask = p.capacity - 1;",
        "    this.chunks = [];",
        "    this.offset = 0;",
        "    const self = this;",
        "    this.port.onmessage = function (event) {",
        "      if (event.data !== 'drop') { self.chunks.push(event.data); return; }",
        "      if (self.index) { Atomics.store(self.index, 1, Atomics.load(self.index, 0)); }",
        "      let dropped = -self.offset;",
        "      for (const chunk of self.chunks) { dropped += chunk.length / self.channels; }",
        "      self.played += dropped;",
        "      self.chunks = [];",
        "      self.offset = 0;",
        "      self.port.postMessage(self.index ? 0 : [self.played, self.short]);",
        "    };",
        "  }",
        "  readRing(out, frames) {",
        "    const read = Atomics.load(this.index, 1);",
        "    const count = Math.min(frames, (Atomics.load(this.index, 0) - read) | 0);",
        "    for (let i = 0; i < count; ++i) {",
        "      const at = ((read + i) & this.mask) * this.channels;",
        "      for (let c = 0; c < out.length; ++c) { out[c][i] = this.data[at + c]; }",
        "    }",
        "    Atomics.store(this.index, 1, (read + count) | 0);",
        "    return count;",
        "  }",
        "  readChunks(out, frames) {",
        "    let i = 0;",
        "    for (; i < frames && this.chunks.length > 0; ++i) {",
        "      const chunk = this.chunks[0];",
        "      for (let c = 0; c < out.length; ++c) { out[c][i] = chunk[this.offset * this.channels + c]; }",
        "      this.offset += 1;",
        "      if (this.offset * this.channels >= chunk.length) { this.chunks.shift(); this.offset = 0; }",
        "    }",
        "    return i;",
        "  }",
        "  process(inputs, outputs) {",
        "    const out = outputs[0];",
        "    const frames = out[0].length;",
        "    const count = this.index ? this.readRing(out, frames) : this.readChunks(out, frames);",
        "    this.played += count;",
        "    if (count < frames) {",
        "      this.short += 1;",
        "      if (this.index) { Atomics.store(this.index, 2, this.short); }",
        "    }",
        "    this.port.postMessage(this.index ? 0 : [this.played, this.short]);",
        "    return true;",
        "  }",
        "}",
        "registerProcessor('maud-stream', MaudStream);",
    ].join("\n");
    const entry = globalThis.maudWeb.contexts[handle];
    entry.worklet = entry.context.audioWorklet.addModule(
        URL.createObjectURL(new Blob([source], {type: "text/javascript"})));
});

// Makes a stream's node once the processor is registered, fills it, and
// returns the node's handle. Each report from the worklet tops the
// stream up to its target, rendering through maudWebRender.
EM_JS(int, maudWebOpenNode, (int handle, void* context, int slot, int channels, int capacity,
                             int quantum), {
    const web = globalThis.maudWeb;
    const entry = web.contexts[handle];
    const rate = entry.context.sampleRate;
    const base = Math.ceil(entry.context.baseLatency * rate / quantum) * quantum;
    const record = {node: null, closed: false, posted: 0, shortSeen: 0, ring: null, index: null, data: null};
    record.target = Math.min(base + 2 * quantum, capacity);
    if (typeof SharedArrayBuffer !== "undefined" && globalThis.crossOriginIsolated) {
        record.ring = new SharedArrayBuffer(12 + capacity * channels * 4);
        record.index = new Int32Array(record.ring, 0, 3);
        record.data = new Float32Array(record.ring, 12);
    }
    web.nodes.push(record);
    const id = web.nodes.length - 1;
    function write(pointer, frames) {
        const from = pointer >> 2;
        if (!record.ring) {
            const samples = HEAPF32.slice(from, from + frames * channels);
            record.node.port.postMessage(samples, [samples.buffer]);
            record.posted += frames;
            return;
        }
        const at = Atomics.load(record.index, 0);
        const start = at & (capacity - 1);
        const first = Math.min(frames, capacity - start);
        record.data.set(HEAPF32.subarray(from, from + first * channels), start * channels);
        record.data.set(HEAPF32.subarray(from + first * channels, from + frames * channels), 0);
        Atomics.store(record.index, 0, (at + frames) | 0);
    }
    function fill(played, short) {
        if (short > record.shortSeen) {
            record.target = Math.min(record.target + (short - record.shortSeen) * quantum, capacity);
            _maudWebXrun(context, slot, short - record.shortSeen);
            record.shortSeen = short;
        }
        const buffered = record.ring ? (Atomics.load(record.index, 0) - Atomics.load(record.index, 1)) | 0
                                     : record.posted - played;
        const frames = Math.floor((record.target - buffered) / quantum) * quantum;
        if (frames > 0) {
            // What plays before these frames: the buffer ahead of them and
            // the context's own latency to the speaker.
            const latency = buffered / rate + entry.context.baseLatency + (entry.context.outputLatency || 0);
            write(_maudWebRender(context, slot, frames, latency), frames);
        }
    }
    entry.worklet.then(function () {
        if (record.closed) {
            return;
        }
        record.node = new AudioWorkletNode(entry.context, "maud-stream", {
            numberOfInputs: 0,
            outputChannelCount: [channels],
            processorOptions: {channels: channels, capacity: capacity, ring: record.ring},
        });
        record.node.port.onmessage = function (event) {
            if (record.closed) {
                return;
            }
            if (record.ring) {
                fill(0, Atomics.load(record.index, 2));
            } else {
                fill(event.data[0], event.data[1]);
            }
        };
        fill(0, 0);
        record.node.connect(entry.context.destination);
    });
    return id;
});

// Drops what a node has queued.
EM_JS(void, maudWebDropNode, (int node), {
    const record = globalThis.maudWeb.nodes[node];
    if (record.node !== null) {
        record.node.port.postMessage("drop");
    }
});

EM_JS(void, maudWebCloseNode, (int node), {
    const record = globalThis.maudWeb.nodes[node];
    record.closed = true;
    if (record.node !== null) {
        record.node.port.onmessage = null;
        record.node.disconnect();
    }
    globalThis.maudWeb.nodes[node] = null;
});

// clang-format on

// Renders frames of a stream, a multiple of the quantum and at most the
// capacity, on the main thread and returns them; the JavaScript side
// calls it to top the stream up, with the seconds until they are heard.
EMSCRIPTEN_KEEPALIVE float* maudWebRender(maudContext* context, int slotIndex, int frames,
                                          double latency);

float* maudWebRender(maudContext* context, int slotIndex, int frames, double latency)
{
    maudWeb* web = context->native;
    maudStreamCore* core = &context->streams.slots[slotIndex].core;
    float* chunk = web->streams[slotIndex].chunk;
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPullPeriod(&core->period, chunk, (uint32_t)frames);
        maudStampOutputClock(core, (int64_t)(latency * 1e9));
        atomic_fetch_add_explicit(&core->position, (uint64_t)frames, memory_order_release);
    }
    else
    {
        memset(chunk, 0, (size_t)frames * core->period.channelCount * sizeof(float));
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    return chunk;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    uint32_t devices = context->def.limits.devices;
    size_t bytes = sizeof(maudWeb) + (size_t)streams * sizeof(maudWebStream) +
                   (size_t)devices * (sizeof(maudDeviceSpec) + sizeof(maudWebEndpoint));
    maudWeb* web = maudContextAllocate(context, bytes, alignof(maudWeb));
    if (web == nullptr)
    {
        return maud_errorCapacity;
    }
    *web = (maudWeb){.handle = maudWebOpen(),
                     .streams = (maudWebStream*)(web + 1),
                     .endpointCapacity = devices,
                     .bytes = bytes};
    memset(web->streams, 0, (size_t)streams * sizeof(maudWebStream));
    web->specs = (maudDeviceSpec*)(web->streams + streams);
    web->endpoints = (maudWebEndpoint*)(web->specs + devices);
    context->native = web;
    if (web->handle == 0)
    {
        maudContextRelease(context, web, bytes, alignof(maudWeb));
        context->native = nullptr;
        return maud_errorUnsupported;
    }
    maudWebAddProcessor(web->handle);
    maudWebAddCaptureProcessor(web->handle);
    uint32_t rate = (uint32_t)maudWebRate(web->handle);
    web->rate = rate;
    maudDeviceSpec spec = {
        .info =
            {
                .direction = maud_directionOutput,
                .nativeLayout = maud_layoutStereo,
                .nativeSampleRate = rate,
                .minSampleRate = rate,
                .maxSampleRate = rate,
            },
        .name = "Default",
        .nameLength = 7,
        .key = "default",
        .keyLength = 7,
    };
    maudDeviceId device;
    maudResult result = maudAddDevice(context, &spec, &device);
    // The microphone, mono until the host asks for more; the browser
    // mixes the track to the stream's channels.
    spec.info.direction = maud_directionInput;
    spec.info.nativeLayout = maud_layoutMono;
    result = result == maud_success ? maudAddDevice(context, &spec, &device) : result;
    context->held = maudWebHeld(web->handle) != 0;
    if (result == maud_success)
    {
        maudWebListDevices(web->handle);
    }
    else
    {
        maudWebClose(web->handle);
        maudContextRelease(context, web, bytes, alignof(maudWeb));
        context->native = nullptr;
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    maudWeb* web = context->native;
    maudWebStopListing(web->handle);
    maudWebClose(web->handle);
    maudContextRelease(context, web, web->bytes, alignof(maudWeb));
    context->native = nullptr;
}

// Follows the browser's hold on the context.
static void Pump(maudContext* context)
{
    maudWeb* web = context->native;
    maudHoldStreams(context, maudWebHeld(web->handle) != 0);
    maudResult result = maudWebSyncDevices(context);
    (void)result;
}

static void ResumeContext(maudContext* context)
{
    maudWeb* web = context->native;
    maudWebResume(web->handle);
}

// Web Audio runs every node at the AudioContext's rate, capture
// included (the browser resamples the microphone): a native stream takes
// it, and a required or converted rate must be it. Its one AudioContext
// plays to one device at a time.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)device;
    uint32_t rate = (uint32_t)maudWebRate(((const maudWeb*)context->native)->handle);
    if (def->mode == maud_modePull ||
        (def->ratePolicy != maud_rateNative && def->sampleRate != rate) ||
        (def->direction == maud_directionOutput && !maudWebSinkFree(context, def->device)))
    {
        return maud_errorUnsupported;
    }
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : MAUD_WEB_QUANTUM,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static maudWebStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudWeb* web = context->native;
    return &web->streams[slot - context->streams.slots];
}

static maudResult AttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWeb* web = context->native;
    maudWebStream* entry = EntryOf(context, slot);
    uint32_t channels = slot->core.period.channelCount;
    entry->chunkBytes = (size_t)MAUD_WEB_CAPACITY * channels * sizeof(float);
    entry->chunk = maudContextAllocate(context, entry->chunkBytes, alignof(float));
    if (entry->chunk == nullptr)
    {
        return maud_errorCapacity;
    }
    if (slot->core.def.direction == maud_directionInput)
    {
        entry->node = maudWebOpenCapture(context, slot, web->handle, entry->chunk);
        return maud_success;
    }
    entry->node = maudWebOpenNode(web->handle, context, (int)(slot - context->streams.slots),
                                  (int)channels, (int)MAUD_WEB_CAPACITY, (int)MAUD_WEB_QUANTUM);
    maudWebSetSink(context, slot);
    return maud_success;
}

static void DetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWebStream* entry = EntryOf(context, slot);
    if (slot->core.def.direction == maud_directionInput)
    {
        maudWebCloseCapture(entry->node);
    }
    else
    {
        maudWebCloseNode(entry->node);
    }
    maudContextRelease(context, entry->chunk, entry->chunkBytes, alignof(float));
    *entry = (maudWebStream){0};
}

// A stopped output renders silence; what it had buffered is dropped. A
// stopped input drops what it captures (maudWebCapture).
static void SetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    if (!active && slot->core.def.direction == maud_directionOutput)
    {
        maudWebDropNode(EntryOf(context, slot)->node);
    }
}

// One AudioContext renders both halves of a duplex stream.
static bool SharesClock(const maudContext* context, const maudStreamSlot* output,
                        const maudStreamSlot* input)
{
    (void)context;
    (void)output;
    (void)input;
    return true;
}

// A hidden tab costs no audio thread: the AudioContext suspends with the
// host.
static void SuspendContext(maudContext* context, bool suspended)
{
    maudWebSuspend(((maudWeb*)context->native)->handle, suspended ? 1 : 0);
}

static const maudBackend s_web = {
    .kind = maud_backendWeb,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = AttachStream,
    .detachStream = DetachStream,
    .setStreamActive = SetStreamActive,
    .retargetStream = nullptr,
    .sharesClock = SharesClock,
    .resumeContext = ResumeContext,
    .suspendContext = SuspendContext,
    .rendersOnCaller = false,
};

const maudBackend* maudGetWebBackend(void)
{
    return &s_web;
}
