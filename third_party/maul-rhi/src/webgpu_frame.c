// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WebGPU frames. A pass's work decides where its commands go: a render
// pass holds a pass with targets, a compute pass holds one without, and
// copies, query resolves and a compute pass's debug labels go on the
// command encoder, so a compute pass leaves its GPU compute pass for
// them and takes up its pipeline, bind groups and immediates again in
// the next. Uploads are written into one staging buffer before the
// frame's commands, which queue order keeps apart from earlier frames';
// readbacks are copied into a mirror of the ring, mapped once the frame
// is submitted. Declared resources come from a pool whose objects wait
// for the frames that took them, dropped when unused for a while; each
// is its own object, never aliased.

#include "webgpu_frame.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "web_js.h"
#include "webgpu_names.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// Where a pass's commands go.
typedef enum Work
{
    WORK_TRANSFER,
    WORK_RENDER,
    WORK_COMPUTE,
} Work;

// clang-format off
// Starts a frame: its command encoder, its uploads written, and the pool
// trimmed of objects no frame took in the last 8.
EM_JS(void, mrhiJsBeginFrame, (int state, double serial, const uint8_t* staging, double stagingBytes,
                           double ringBytes), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const device = self.device;
    self.pool = self.pool.filter(entry => {
        if (entry.used + 8 >= serial) {
            return true;
        }
        entry.object.destroy();
        return false;
    });
    const frame = {serial, encoder: device.createCommandEncoder(), objects: [null], taken: [],
                   targets: [], depth: undefined, work: 0, pass: null, ending: null,
                   labelled: false, bound: null, entries: [], occlusion: null,
                   written: new Map(), readback: null, low: ringBytes, high: 0, ranges: []};
    // The queries of each set the frame has written so far.
    frame.write = (set, query) => {
        if (!frame.written.has(set)) {
            frame.written.set(set, new Set());
        }
        frame.written.get(set).add(query);
    };
    // A pooled object, or a new one.
    frame.take = (key, make) => {
        const at = self.pool.findIndex(entry => entry.key === key);
        const entry = at >= 0 ? self.pool.splice(at, 1)[0] : {key, object: make(), used: 0};
        frame.taken.push(entry);
        return entry.object;
    };
    frame.view = (index, mip, layer, volume) => frame.objects[index].createView({
        dimension: volume ? '3d' : '2d',
        baseMipLevel: mip,
        mipLevelCount: 1,
        baseArrayLayer: volume ? 0 : layer,
        arrayLayerCount: 1,
    });
    // The pass draws and dispatches go to, a compute pass taking up what
    // its last one had bound.
    frame.inside = () => {
        if (!frame.pass) {
            const pass = frame.encoder.beginComputePass();
            const bound = frame.bound;
            if (bound.pipeline) {
                pass.setPipeline(bound.pipeline);
            }
            bound.groups.forEach((group, table) => pass.setBindGroup(table, group));
            if (bound.high > 0) {
                pass.setImmediates(0, bound.immediates.subarray(0, bound.high));
            }
            frame.pass = pass;
        }
        return frame.pass;
    };
    // The command encoder, a compute pass left for it.
    frame.outside = () => {
        if (frame.work === 2 && frame.pass) {
            frame.pass.end();
            frame.pass = null;
        }
        return frame.encoder;
    };
    // The ring's mirror, which readbacks copy into.
    frame.ring = () => {
        if (!frame.readback) {
            frame.readback = self.readbacks.pop() || device.createBuffer({
                size: ringBytes,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
            });
        }
        return frame.readback;
    };
    frame.note = (offset, bytes) => {
        frame.ranges.push([offset, bytes]);
        frame.low = Math.min(frame.low, offset);
        frame.high = Math.max(frame.high, offset + bytes);
    };
    if (stagingBytes > 0) {
        const size = Math.ceil(stagingBytes / 4) * 4;
        if (!self.staging || self.staging.size < size) {
            // The old buffer waits for the frames that read it.
            if (self.staging) {
                self.retiring.push([serial, gpu.put(self, self.staging)]);
            }
            self.staging = device.createBuffer({
                size: Math.ceil(size / 65536) * 65536,
                usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
            });
        }
        device.queue.writeBuffer(self.staging, 0, HEAPU8, staging, size);
    }
    self.frame = frame;
});

EM_JS(void, mrhiJsUseObject, (int state, uint32_t index, int handle), {
    const self = Module.mrhiGpu.states[state];
    self.frame.objects[index] = self.objects[handle];
});

// A canvas's texture, whose handle is freed as the frame takes it: the
// browser presents it once the page returns to its event loop.
EM_JS(void, mrhiJsUseImage, (int state, uint32_t index, int handle), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    self.frame.objects[index] = gpu.take(self, handle);
});

EM_JS(void, mrhiJsTransientBuffer, (int state, uint32_t index, double size, uint32_t usage), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const key = ['buffer', size, usage].join();
    self.frame.objects[index] = self.frame.take(key, () => gpu.buffer(self.device, size, usage));
});

EM_JS(void, mrhiJsTransientTexture, (int state, uint32_t index, bool volume, const char* format,
                                 uint32_t width, uint32_t height, uint32_t depth, uint32_t mips,
                                 uint32_t samples, uint32_t usage, const char* viewFormats), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const name = UTF8ToString(format);
    const views = UTF8ToString(viewFormats);
    const key = ['texture', volume, name, width, height, depth, mips, samples, usage, views].join();
    self.frame.objects[index] = self.frame.take(key, () => gpu.texture(
        self.device, volume, name, width, height, depth, mips, samples, usage, views));
});

EM_JS(void, mrhiJsAddNoTarget, (int state), {
    Module.mrhiGpu.states[state].frame.targets.push(null);
});

EM_JS(void, mrhiJsAddColorTarget, (int state, uint32_t resource, uint32_t mip, uint32_t layer,
                            bool volume, bool keep, bool discard, float red, float green,
                            float blue, float alpha, uint32_t resolve, uint32_t resolveMip,
                            uint32_t resolveLayer), {
    const frame = Module.mrhiGpu.states[state].frame;
    const target = {
        view: frame.view(resource, mip, layer, volume),
        loadOp: keep ? 'load' : 'clear',
        storeOp: discard ? 'discard' : 'store',
        clearValue: [red, green, blue, alpha],
    };
    if (volume) {
        target.depthSlice = layer;
    }
    if (resolve) {
        target.resolveTarget = frame.view(resolve, resolveMip, resolveLayer, false);
    }
    frame.targets.push(target);
});

// A depth target: the ops of the aspects its format has, or those
// aspects read-only.
EM_JS(void, mrhiJsAddDepthTarget, (int state, uint32_t resource, uint32_t mip, uint32_t layer,
                            bool depth, bool stencil, bool readOnly, bool depthKeep,
                            bool depthDiscard, float clearDepth, bool stencilKeep,
                            bool stencilDiscard, uint32_t clearStencil), {
    const frame = Module.mrhiGpu.states[state].frame;
    const target = {view: frame.view(resource, mip, layer, false)};
    if (depth && readOnly) {
        target.depthReadOnly = true;
    } else if (depth) {
        target.depthLoadOp = depthKeep ? 'load' : 'clear';
        target.depthStoreOp = depthDiscard ? 'discard' : 'store';
        target.depthClearValue = clearDepth;
    }
    if (stencil && readOnly) {
        target.stencilReadOnly = true;
    } else if (stencil) {
        target.stencilLoadOp = stencilKeep ? 'load' : 'clear';
        target.stencilStoreOp = stencilDiscard ? 'discard' : 'store';
        target.stencilClearValue = clearStencil >>> 0;
    }
    frame.depth = target;
});

// Begins a pass: its label a debug group on the encoder, a render pass
// with the targets given, a compute pass at once only for its
// timestamps. A compute pass that leaves for the encoder writes its end
// timestamp in a pass of its own.
EM_JS(void, mrhiJsBeginPass, (int state, int work, const char* label, int labelLength,
                          int occlusion, int timestamps, uint32_t begin, uint32_t end,
                          bool split), {
    const self = Module.mrhiGpu.states[state];
    const frame = self.frame;
    const none = 0xffffffff;
    begin >>>= 0;
    end >>>= 0;
    frame.work = work;
    frame.labelled = labelLength > 0;
    if (frame.labelled) {
        frame.encoder.pushDebugGroup(UTF8ToString(label, labelLength));
    }
    frame.bound = {pipeline: null, entry: null, groups: [], high: 0,
                   immediates: new Uint8Array(self.device.limits.maxImmediateSize)};
    const writes = timestamps ? {querySet: self.objects[timestamps]} : null;
    [begin, end].forEach(query => writes && query !== none && frame.write(writes.querySet, query));
    if (writes && begin !== none) {
        writes.beginningOfPassWriteIndex = begin;
    }
    if (writes && end !== none && !(work === 2 && split)) {
        writes.endOfPassWriteIndex = end;
    } else if (writes && end !== none) {
        frame.ending = {querySet: writes.querySet, endOfPassWriteIndex: end};
    }
    if (work === 1) {
        const descriptor = {colorAttachments: frame.targets, depthStencilAttachment: frame.depth};
        frame.occlusion = occlusion ? self.objects[occlusion] : null;
        if (frame.occlusion) {
            descriptor.occlusionQuerySet = frame.occlusion;
        }
        if (writes) {
            descriptor.timestampWrites = writes;
        }
        frame.pass = frame.encoder.beginRenderPass(descriptor);
        frame.targets = [];
        frame.depth = undefined;
    } else if (work === 2 && writes && Object.keys(writes).length > 1) {
        frame.pass = frame.encoder.beginComputePass({timestampWrites: writes});
    }
});

EM_JS(void, mrhiJsEndPass, (int state), {
    const frame = Module.mrhiGpu.states[state].frame;
    if (frame.pass) {
        frame.pass.end();
        frame.pass = null;
    }
    if (frame.ending) {
        frame.encoder.beginComputePass({timestampWrites: frame.ending}).end();
        frame.ending = null;
    }
    if (frame.labelled) {
        frame.encoder.popDebugGroup();
    }
});

// Sets a pipeline, and the empty bind groups of its tables without
// bindings.
EM_JS(void, mrhiJsSetPipeline, (int state, int handle), {
    const self = Module.mrhiGpu.states[state];
    const frame = self.frame;
    const entry = self.objects[handle];
    const pass = frame.inside();
    pass.setPipeline(entry.pipeline);
    frame.bound.pipeline = entry.pipeline;
    frame.bound.entry = entry;
    entry.empty.forEach((group, table) => {
        if (group) {
            pass.setBindGroup(table, group);
            frame.bound.groups[table] = group;
        }
    });
});

EM_JS(void, mrhiJsImmediates, (int state, uint32_t offset, const uint8_t* bytes, uint32_t size), {
    const frame = Module.mrhiGpu.states[state].frame;
    const bound = frame.bound;
    bound.immediates.set(HEAPU8.subarray(bytes, bytes + size), offset);
    bound.high = Math.max(bound.high, offset + size);
    frame.inside().setImmediates(offset, bound.immediates.subarray(offset, offset + size));
});

EM_JS(void, mrhiJsBindBuffer, (int state, uint32_t slot, uint32_t resource, double offset,
                           double size), {
    const frame = Module.mrhiGpu.states[state].frame;
    frame.entries.push({binding: slot, resource: {buffer: frame.objects[resource], offset, size}});
});

EM_JS(void, mrhiJsBindSampler, (int state, uint32_t slot, int handle), {
    const self = Module.mrhiGpu.states[state];
    self.frame.entries.push({binding: slot, resource: self.objects[handle]});
});

// A texture's binding, through a view of one aspect's format when it
// names one, which the browser resolves.
EM_JS(void, mrhiJsBindTexture, (int state, uint32_t slot, uint32_t resource, const char* format,
                            uint32_t dimension, uint32_t aspect, uint32_t baseMip, uint32_t mips,
                            uint32_t baseLayer, uint32_t layers), {
    const gpu = Module.mrhiGpu;
    const frame = gpu.states[state].frame;
    const descriptor = {
        dimension: gpu.names.dimensions[dimension],
        aspect: gpu.names.aspects[aspect],
        baseMipLevel: baseMip,
        mipLevelCount: mips,
        baseArrayLayer: baseLayer,
        arrayLayerCount: layers,
    };
    if (aspect === 0) {
        descriptor.format = UTF8ToString(format);
    }
    frame.entries.push({binding: slot, resource: frame.objects[resource].createView(descriptor)});
});

// Makes a bind group of the bindings given, in the pipeline's layout of
// the table, and sets it.
EM_JS(void, mrhiJsBindTable, (int state, uint32_t table), {
    const self = Module.mrhiGpu.states[state];
    const frame = self.frame;
    const bound = frame.bound;
    const pass = frame.inside();
    const group = self.device.createBindGroup({layout: bound.entry.layouts[table],
                                               entries: frame.entries});
    frame.entries = [];
    pass.setBindGroup(table, group);
    bound.groups[table] = group;
});

EM_JS(void, mrhiJsSetVertexBuffer, (int state, uint32_t slot, uint32_t resource, double offset,
                             double size), {
    const frame = Module.mrhiGpu.states[state].frame;
    frame.pass.setVertexBuffer(slot, frame.objects[resource], offset, size);
});

EM_JS(void, mrhiJsSetIndexBuffer, (int state, bool wide, uint32_t resource, double offset,
                            double size), {
    const frame = Module.mrhiGpu.states[state].frame;
    frame.pass.setIndexBuffer(frame.objects[resource], wide ? 'uint32' : 'uint16', offset, size);
});

EM_JS(void, mrhiJsViewport, (int state, float x, float y, float width, float height, float minDepth,
                         float maxDepth), {
    Module.mrhiGpu.states[state].frame.pass.setViewport(x, y, width, height, minDepth, maxDepth);
});

EM_JS(void, mrhiJsScissor, (int state, uint32_t x, uint32_t y, uint32_t width, uint32_t height), {
    Module.mrhiGpu.states[state].frame.pass.setScissorRect(x, y, width, height);
});

EM_JS(void, mrhiJsBlendConstant, (int state, float red, float green, float blue, float alpha), {
    Module.mrhiGpu.states[state].frame.pass.setBlendConstant([red, green, blue, alpha]);
});

EM_JS(void, mrhiJsStencilReference, (int state, uint32_t reference), {
    Module.mrhiGpu.states[state].frame.pass.setStencilReference(reference >>> 0);
});

EM_JS(void, mrhiJsDraw, (int state, uint32_t vertices, uint32_t instances, uint32_t first,
                     uint32_t firstInstance), {
    Module.mrhiGpu.states[state].frame.pass.draw(vertices >>> 0, instances >>> 0, first >>> 0,
                                                 firstInstance >>> 0);
});

EM_JS(void, mrhiJsDrawIndexed, (int state, uint32_t indices, uint32_t instances, uint32_t first,
                            int32_t baseVertex, uint32_t firstInstance), {
    Module.mrhiGpu.states[state].frame.pass.drawIndexed(indices >>> 0, instances >>> 0,
                                                        first >>> 0, baseVertex,
                                                        firstInstance >>> 0);
});

EM_JS(void, mrhiJsDispatch, (int state, uint32_t x, uint32_t y, uint32_t z), {
    Module.mrhiGpu.states[state].frame.inside().dispatchWorkgroups(x >>> 0, y >>> 0, z >>> 0);
});

// A draw, an indexed draw or a dispatch, its arguments read on the GPU.
EM_JS(void, mrhiJsIndirect, (int state, int kind, uint32_t resource, double offset), {
    const frame = Module.mrhiGpu.states[state].frame;
    const buffer = frame.objects[resource];
    if (kind === 0) {
        frame.pass.drawIndirect(buffer, offset);
    } else if (kind === 1) {
        frame.pass.drawIndexedIndirect(buffer, offset);
    } else {
        frame.inside().dispatchWorkgroupsIndirect(buffer, offset);
    }
});

EM_JS(void, mrhiJsOcclusion, (int state, bool begin, uint32_t query), {
    const frame = Module.mrhiGpu.states[state].frame;
    if (begin) {
        frame.write(frame.occlusion, query);
        frame.pass.beginOcclusionQuery(query);
    } else {
        frame.pass.endOcclusionQuery();
    }
});

// Resolves queries; a query set keeps its values across frames, so those
// the frame has not written are cleared to 0 after.
EM_JS(void, mrhiJsResolve, (int state, int set, uint32_t first, uint32_t count, uint32_t resource,
                        double offset), {
    const self = Module.mrhiGpu.states[state];
    const frame = self.frame;
    const querySet = self.objects[set];
    const buffer = frame.objects[resource];
    const encoder = frame.outside();
    encoder.resolveQuerySet(querySet, first, count, buffer, offset);
    const written = frame.written.get(querySet) || new Set();
    let run = 0;
    for (let query = first; query <= first + count; ++query) {
        if (query < first + count && !written.has(query)) {
            run += 1;
        } else if (run > 0) {
            encoder.clearBuffer(buffer, offset + 8 * (query - first - run), 8 * run);
            run = 0;
        }
    }
});

// Pushes a debug group, pops one, or inserts a marker.
EM_JS(void, mrhiJsDebug, (int state, int kind, const char* label, int labelLength), {
    const frame = Module.mrhiGpu.states[state].frame;
    const target = frame.work === 1 ? frame.pass : frame.outside();
    if (kind === 0) {
        target.pushDebugGroup(UTF8ToString(label, labelLength));
    } else if (kind === 1) {
        target.popDebugGroup();
    } else {
        target.insertDebugMarker(UTF8ToString(label, labelLength));
    }
});

// A copy between buffers, object 0 naming the staging buffer as the
// source and the ring's mirror as the destination.
EM_JS(void, mrhiJsCopyBuffer, (int state, uint32_t source, double sourceOffset, uint32_t target,
                           double targetOffset, double size), {
    const self = Module.mrhiGpu.states[state];
    const frame = self.frame;
    const from = source ? frame.objects[source] : self.staging;
    const to = target ? frame.objects[target] : frame.ring();
    frame.outside().copyBufferToBuffer(from, sourceOffset, to, targetOffset, size);
    if (!target) {
        frame.note(targetOffset, size);
    }
});

// Zeros a range of a frame buffer.
EM_JS(void, mrhiJsClearBuffer, (int state, uint32_t object, double offset, double size), {
    const frame = Module.mrhiGpu.states[state].frame;
    frame.outside().clearBuffer(frame.objects[object], offset, size);
});

// A copy between a buffer and a texture, object 0 naming staging or the
// ring's mirror, bytes the span a readback fills.
EM_JS(void, mrhiJsCopyWithTexture, (int state, bool toTexture, uint32_t buffer, double offset,
                                uint32_t bytesPerRow, uint32_t rowsPerImage, uint32_t texture,
                                uint32_t mip, uint32_t x, uint32_t y, uint32_t z, uint32_t aspect,
                                uint32_t width, uint32_t height, uint32_t depth, double bytes), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const frame = self.frame;
    const ring = !toTexture && !buffer;
    // A layout of 0, for a copy of one row or one layer, is left out,
    // as WebGPU takes it.
    const side = {
        buffer: buffer ? frame.objects[buffer] : (toTexture ? self.staging : frame.ring()),
        offset,
        bytesPerRow: bytesPerRow || undefined,
        rowsPerImage: rowsPerImage || undefined,
    };
    const image = {texture: frame.objects[texture], mipLevel: mip, origin: {x, y, z},
                   aspect: gpu.names.aspects[aspect]};
    const encoder = frame.outside();
    if (toTexture) {
        encoder.copyBufferToTexture(side, image, [width, height, depth]);
    } else {
        encoder.copyTextureToBuffer(image, side, [width, height, depth]);
    }
    if (ring) {
        frame.note(offset, bytes);
    }
});

EM_JS(void, mrhiJsCopyTexture, (int state, const uint32_t* source, const uint32_t* target,
                            uint32_t width, uint32_t height, uint32_t depth), {
    const gpu = Module.mrhiGpu;
    const frame = gpu.states[state].frame;
    const side = at => {
        const word = index => HEAPU32[(at >> 2) + index];
        return {texture: frame.objects[word(0)], mipLevel: word(1),
                origin: {x: word(2), y: word(3), z: word(4)}, aspect: gpu.names.aspects[word(5)]};
    };
    frame.outside().copyTextureToTexture(side(source), side(target), [width, height, depth]);
});

// Submits the frame; it finishes when its readbacks are in the ring, or
// when the queue has done its work, and the frames finished advance to
// the first still running.
EM_JS(void, mrhiJsSubmit, (int state, uint8_t* ring), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const frame = self.frame;
    self.frame = null;
    self.device.queue.submit([frame.encoder.finish()]);
    self.running.push(frame);
    const settle = () => {
        frame.done = true;
        frame.taken.forEach(entry => {
            entry.used = frame.serial;
            self.pool.push(entry);
        });
        if (frame.readback) {
            self.readbacks.push(frame.readback);
        }
        while (self.running.length > 0 && self.running[0].done) {
            self.finished = self.running.shift().serial;
        }
    };
    if (!frame.readback) {
        self.device.queue.onSubmittedWorkDone().then(settle, settle);
        return;
    }
    const low = frame.low;
    const size = Math.ceil((frame.high - low) / 4) * 4;
    frame.readback.mapAsync(GPUMapMode.READ, low, size).then(() => {
        if (gpu.states[state] === self) {
            const mapped = new Uint8Array(frame.readback.getMappedRange(low, size));
            for (const [offset, bytes] of frame.ranges) {
                HEAPU8.set(mapped.subarray(offset - low, offset - low + bytes), ring + offset);
            }
        }
        frame.readback.unmap();
        settle();
    }, settle);
});

EM_JS(double, mrhiJsFinished, (int state), {
    return Module.mrhiGpu.states[state].finished;
});
// clang-format on

EM_JS_DEPS(mrhi_webgpu_frame, "$UTF8ToString");

static Work WorkOf(const mrhiDriverPass* pass)
{
    if (pass->passClass == mrhi_passTransfer)
    {
        return WORK_TRANSFER;
    }
    bool targets = pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0;
    return targets ? WORK_RENDER : WORK_COMPUTE;
}

static void AddTransientTexture(int state, uint32_t index1, const mrhiDriverResource* resource)
{
    const mrhiTextureDef* def = resource->texture;
    // The usage the frame's passes gave it, transient among them.
    mrhiTextureDef used = *def;
    used.usage = resource->usage;
    char viewFormats[MRHI_WEBGPU_VIEW_FORMAT_BYTES];
    mrhiWebGpuViewFormats(&used, viewFormats);
    mrhiJsTransientTexture(state, index1, def->kind == mrhi_texture3d,
                           mrhiWebGpuFormat(def->format), def->width, def->height,
                           def->depthOrLayers, def->mipLevels, def->sampleCount, resource->usage,
                           viewFormats);
}

// Names the frame's needed resources on the JavaScript side by slot plus
// one.
static void AddResources(int state, const mrhiDriverFrame* frame)
{
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        // A canvas's texture is taken even when no pass writes it, since
        // the browser presents it either way.
        if (!resource->needed && resource->kind != mrhiDriverSurfaceImage)
        {
            continue;
        }
        switch (resource->kind)
        {
        case mrhiDriverDeviceTexture:
        case mrhiDriverDeviceBuffer:
            mrhiJsUseObject(state, i + 1, (int)resource->handle);
            break;
        case mrhiDriverTransientBuffer:
            mrhiJsTransientBuffer(state, i + 1, (double)resource->size, resource->usage);
            break;
        case mrhiDriverTransientTexture:
            AddTransientTexture(state, i + 1, resource);
            break;
        default:
            MRHI_ASSERT(resource->kind == mrhiDriverSurfaceImage);
            mrhiJsUseImage(state, i + 1, (int)resource->image);
            break;
        }
    }
}

// The def of a frame's texture, by slot plus one.
static const mrhiTextureDef* TextureOf(const mrhiDriverFrame* frame, uint32_t index1)
{
    MRHI_ASSERT(index1 != 0 && index1 <= frame->resourceCount);
    return frame->resources[index1 - 1].texture;
}

static void AddTargets(int state, const mrhiDriverFrame* frame, const mrhiDriverPass* pass)
{
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        const mrhiColorTarget* target = &pass->colorTargets[i];
        if (target->resource.index1 == 0)
        {
            mrhiJsAddNoTarget(state);
            continue;
        }
        const mrhiClearColor* clear = &target->clear;
        mrhiJsAddColorTarget(state, target->resource.index1, target->mip, target->layer,
                             TextureOf(frame, target->resource.index1)->kind == mrhi_texture3d,
                             target->load == mrhi_loadKeep,
                             pass->colorStores[i] == mrhi_storeDiscard, clear->red, clear->green,
                             clear->blue, clear->alpha, target->resolve.index1, target->resolveMip,
                             target->resolveLayer);
    }
    const mrhiDepthTarget* depth = &pass->depthTarget;
    if (depth->resource.index1 == 0)
    {
        return;
    }
    mrhiFormat format = TextureOf(frame, depth->resource.index1)->format;
    mrhiJsAddDepthTarget(state, depth->resource.index1, depth->mip, depth->layer,
                         mrhiFormatHasDepth(format), mrhiFormatHasStencil(format), depth->readOnly,
                         depth->depthLoad == mrhi_loadKeep, pass->depthStore == mrhi_storeDiscard,
                         depth->clearDepth, depth->stencilLoad == mrhi_loadKeep,
                         pass->stencilStore == mrhi_storeDiscard, depth->clearStencil);
}

// Whether a command goes on the command encoder, out of a compute pass.
static bool IsOutside(const mrhiCommand* command)
{
    return (command->type >= mrhiCommandPushDebugGroup &&
            command->type <= mrhiCommandDebugMarker) ||
           command->type >= mrhiCommandResolveQueries;
}

// Whether a pass has a command out of a compute pass.
static bool Splits(const mrhiDriverFrame* frame, const mrhiDriverPass* pass)
{
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            if (IsOutside(&at->commands[i]))
            {
                return true;
            }
        }
    }
    return false;
}

static void Bind(int state, const mrhiCommand* command)
{
    uint32_t count = (uint32_t)command->b;
    MRHI_ASSERT(count <= MRHI_TABLE_BINDINGS);
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiCommandBinding binding;
        memcpy(&binding, &command[1 + i], sizeof(binding));
        switch (binding.kind)
        {
        case mrhi_bindingUniformBuffer:
        case mrhi_bindingStorageBuffer:
        case mrhi_bindingReadOnlyStorageBuffer:
            mrhiJsBindBuffer(state, binding.slot, binding.object, (double)binding.offset,
                             (double)binding.size);
            break;
        case mrhi_bindingSampler:
            mrhiJsBindSampler(state, binding.slot, (int)binding.offset);
            break;
        default:
            mrhiJsBindTexture(state, binding.slot, binding.object,
                              mrhiWebGpuFormat(binding.viewFormat), binding.viewKind,
                              binding.aspect, binding.baseMip, binding.mipCount,
                              (uint32_t)binding.offset, (uint32_t)binding.size);
            break;
        }
    }
    mrhiJsBindTable(state, command->a);
}

// Sets the root block or render state from a command and its payload.
static void SetState(int state, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandRootBlock:
        mrhiJsImmediates(state, command->a, (const uint8_t*)&command[1], (uint32_t)command->b);
        break;
    case mrhiCommandViewport:
    {
        mrhiViewport viewport;
        memcpy(&viewport, &command[1], sizeof(viewport));
        mrhiJsViewport(state, viewport.x, viewport.y, viewport.width, viewport.height,
                       viewport.minDepth, viewport.maxDepth);
        break;
    }
    case mrhiCommandScissor:
        mrhiJsScissor(state, command->a, (uint32_t)command->b, (uint32_t)command->c,
                      (uint32_t)command->d);
        break;
    case mrhiCommandBlendConstant:
    {
        mrhiClearColor color;
        memcpy(&color, &command[1], sizeof(color));
        mrhiJsBlendConstant(state, color.red, color.green, color.blue, color.alpha);
        break;
    }
    default:
        MRHI_ASSERT(command->type == mrhiCommandStencilReference);
        mrhiJsStencilReference(state, command->a);
        break;
    }
}

static void Draw(int state, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandDraw:
        mrhiJsDraw(state, command->a, (uint32_t)command->b, (uint32_t)command->c,
                   (uint32_t)command->d);
        break;
    case mrhiCommandDrawIndexed:
        mrhiJsDrawIndexed(state, command->a, (uint32_t)command->b, (uint32_t)command->c,
                          (int32_t)(uint32_t)(command->c >> 32), (uint32_t)command->d);
        break;
    case mrhiCommandDispatch:
        mrhiJsDispatch(state, command->a, (uint32_t)command->b, (uint32_t)command->c);
        break;
    case mrhiCommandVertexBuffer:
        mrhiJsSetVertexBuffer(state, command->a, (uint32_t)command->b, (double)command->c,
                              (double)command->d);
        break;
    case mrhiCommandIndexBuffer:
        mrhiJsSetIndexBuffer(state, command->a == mrhi_indexUint32, (uint32_t)command->b,
                             (double)command->c, (double)command->d);
        break;
    default:
        MRHI_ASSERT(command->type >= mrhiCommandDrawIndirect &&
                    command->type <= mrhiCommandDispatchIndirect);
        mrhiJsIndirect(state, command->type - mrhiCommandDrawIndirect, command->a,
                       (double)command->c);
        break;
    }
}

// The bytes a copy into a buffer spans there, its last row only as long
// as the texels it holds.
static uint64_t SpanOf(const mrhiTextureDef* def, const mrhiCommandBufferSide* buffer,
                       const mrhiCommandTextureSide* texture, const mrhiCommand* command)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t texelBytes = mrhiGetFormatCopy(def->format, texture->aspect).bytes;
    MRHI_ASSERT(texelBytes > 0);
    uint64_t rows = (uint32_t)command->c / block.height;
    uint64_t rowBytes = (uint64_t)((uint32_t)command->b / block.width) * texelBytes;
    uint64_t depth = (uint32_t)command->d;
    return (uint64_t)buffer->bytesPerRow * buffer->rowsPerImage * (depth - 1) +
           (uint64_t)buffer->bytesPerRow * (rows - 1) + rowBytes;
}

static void CopyWithTexture(int state, const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    bool toTexture =
        command->type == mrhiCommandCopyBufferToTexture || command->type == mrhiCommandWriteTexture;
    mrhiCommandBufferSide buffer;
    mrhiCommandTextureSide texture;
    memcpy(&buffer, toTexture ? &command[1] : &command[2], sizeof(buffer));
    memcpy(&texture, toTexture ? &command[2] : &command[1], sizeof(texture));
    uint64_t bytes = command->type == mrhiCommandReadTexture
                         ? SpanOf(TextureOf(frame, texture.object), &buffer, &texture, command)
                         : 0;
    mrhiJsCopyWithTexture(state, toTexture, buffer.object, (double)buffer.offset,
                          buffer.bytesPerRow, buffer.rowsPerImage, texture.object, texture.mip,
                          texture.x, texture.y, texture.z, texture.aspect, (uint32_t)command->b,
                          (uint32_t)command->c, (uint32_t)command->d, (double)bytes);
}

static void Copy(int state, const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandCopyBuffer:
    case mrhiCommandWriteBuffer:
    case mrhiCommandReadBuffer:
    {
        mrhiCommandBufferSide sides[2];
        memcpy(sides, &command[1], sizeof(sides));
        mrhiJsCopyBuffer(state, sides[0].object, (double)sides[0].offset, sides[1].object,
                         (double)sides[1].offset, (double)command->b);
        break;
    }
    case mrhiCommandCopyTexture:
    {
        mrhiCommandTextureSide sides[2];
        memcpy(sides, &command[1], sizeof(sides));
        const uint32_t source[6] = {sides[0].object, sides[0].mip, sides[0].x,
                                    sides[0].y,      sides[0].z,   sides[0].aspect};
        const uint32_t target[6] = {sides[1].object, sides[1].mip, sides[1].x,
                                    sides[1].y,      sides[1].z,   sides[1].aspect};
        mrhiJsCopyTexture(state, source, target, (uint32_t)command->b, (uint32_t)command->c,
                          (uint32_t)command->d);
        break;
    }
    case mrhiCommandClearBuffer:
        mrhiJsClearBuffer(state, command->a, (double)command->c, (double)command->d);
        break;
    default:
        CopyWithTexture(state, frame, command);
        break;
    }
}

static void Encode(int state, const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandGraphicsPipeline:
    case mrhiCommandComputePipeline:
        mrhiJsSetPipeline(state, (int)command->b);
        break;
    case mrhiCommandRootBlock:
    case mrhiCommandViewport:
    case mrhiCommandScissor:
    case mrhiCommandBlendConstant:
    case mrhiCommandStencilReference:
        SetState(state, command);
        break;
    case mrhiCommandBindings:
        Bind(state, command);
        break;
    case mrhiCommandPushDebugGroup:
    case mrhiCommandDebugMarker:
        mrhiJsDebug(state, command->type == mrhiCommandPushDebugGroup ? 0 : 2,
                    (const char*)&command[1], (int)command->b);
        break;
    case mrhiCommandPopDebugGroup:
        mrhiJsDebug(state, 1, nullptr, 0);
        break;
    case mrhiCommandBeginOcclusionQuery:
    case mrhiCommandEndOcclusionQuery:
        mrhiJsOcclusion(state, command->type == mrhiCommandBeginOcclusionQuery, command->a);
        break;
    case mrhiCommandResolveQueries:
        mrhiJsResolve(state, (int)command->b, (uint32_t)command->c, (uint32_t)(command->c >> 32),
                      command->a, (double)command->d);
        break;
    default:
        if (command->type >= mrhiCommandCopyBuffer)
        {
            Copy(state, frame, command);
        }
        else
        {
            Draw(state, command);
        }
        break;
    }
}

static void EncodePass(int state, const mrhiDriverFrame* frame, const mrhiDriverPass* pass)
{
    // WebGPU has no heaps, so no pass names one.
    MRHI_ASSERT(pass->heap == 0);
    Work work = WorkOf(pass);
    if (work == WORK_RENDER)
    {
        AddTargets(state, frame, pass);
    }
    mrhiJsBeginPass(state, (int)work, pass->label, (int)pass->labelLength, (int)pass->occlusionSet,
                    (int)pass->timestampSet, pass->timestampBegin, pass->timestampEnd,
                    work == WORK_COMPUTE && Splits(frame, pass));
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            Encode(state, frame, &at->commands[i]);
        }
    }
    mrhiJsEndPass(state);
}

void mrhiWebGpuSubmitFrame(int state, const mrhiDriverFrame* frame, uint64_t serial)
{
    mrhiJsBeginFrame(state, (double)serial, frame->staging, (double)frame->stagingBytes,
                     (double)frame->readbackBytes);
    AddResources(state, frame);
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        EncodePass(state, frame, &frame->passes[p]);
    }
    mrhiJsSubmit(state, frame->readbackRing);
}

uint64_t mrhiWebGpuFinishedFrames(int state)
{
    return (uint64_t)mrhiJsFinished(state);
}
