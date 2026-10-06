// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The driver SPI at the instance level (mrhi-0003), as the core calls
// it. A driver reports finished work only when the core polls it, and
// never calls the core. Installed as maul-rhi/spi/driver.h, with
// command.h, reflection.h and container.h, for drivers built outside
// the tree (mrhi-0024): their types are the SPI, while the core
// functions they declare are not.

#ifndef MAUL_RHI_SRC_DRIVER_H
#define MAUL_RHI_SRC_DRIVER_H

#include "command.h"
#include "reflection.h"

#include "maul-rhi/heap.h"
#include "maul-rhi/pipeline.h"

// The SPI version a driver's vtable must carry. Any change to the SPI
// headers raises it (mrhi-0024); 4 is the first installed.
#define MRHI_SPI_VERSION 4

// The 64-bit counters a pipeline statistics query writes (mrhi-0023).
#define MRHI_STATISTICS_COUNTERS 11

// The bytes a resolve writes per query of a type: eleven counters for a
// statistics query, one value for another.
static inline uint64_t mrhiQueryBytes(mrhiQueryType type)
{
    return (type == mrhi_queryPipelineStatistics ? MRHI_STATISTICS_COUNTERS : 1u) *
           sizeof(uint64_t);
}

// An adapter as a driver reports it: its handle, never zero, its facts,
// and the features and limits it can grant.
typedef struct mrhiDriverAdapter
{
    uint64_t handle;
    mrhiAdapterInfo info;
    mrhiFeatures features;
    mrhiLimits limits;
} mrhiDriverAdapter;

// Finished work: the tag the core gave the request, and its outcome; or,
// with tag 0 and mrhi_errorDeviceLost, the device's loss.
typedef struct mrhiDriverEvent
{
    uint64_t tag;
    mrhiResult outcome;
} mrhiDriverEvent;

// A compute pipeline the core has checked, as its driver makes it: its
// label, its shader's driver handle and reflection, the entry point
// there, and the program's constant values, each id known and at most
// once, with every required one given.
typedef struct mrhiDriverComputePipeline
{
    const char* label;
    size_t labelLength;
    uint64_t shader;
    const mrhiReflection* reflection;
    uint32_t entry;
    const mrhiConstantValue* constants;
    uint32_t constantCount;
} mrhiDriverComputePipeline;

// A graphics pipeline the core has checked, as its driver makes it: its
// shader's driver handle and reflection, its entry points there (the
// fragment entry entryCount for none), and its def.
typedef struct mrhiDriverGraphicsPipeline
{
    uint64_t shader;
    const mrhiReflection* reflection;
    uint32_t vertexEntry;
    uint32_t fragmentEntry;
    const mrhiGraphicsPipelineDef* def;
} mrhiDriverGraphicsPipeline;

// What a submitted frame's resource is to the driver.
typedef enum mrhiDriverResourceKind
{
    // A texture or buffer the frame makes in its memory.
    mrhiDriverTransientTexture,
    mrhiDriverTransientBuffer,
    // A device texture or buffer, by its handle.
    mrhiDriverDeviceTexture,
    mrhiDriverDeviceBuffer,
    // An image acquired from a swapchain, by the swapchain's handle and
    // the image, presented after the frame's work.
    mrhiDriverSurfaceImage,
} mrhiDriverResourceKind;

// A resource of a submitted frame: its kind; whether a kept pass uses it
// (the driver makes nothing for one no pass does); a device object's or
// swapchain's handle (0 for a transient); a surface image's image; a
// texture's def, whose usage is the usage field for a transient; a
// buffer's bytes; its usage, a transient's the one its passes derive;
// where a transient lives in the frame's memory; and whether a device
// object began the frame sealed (mrhi-0015), which a heap may read it in
// without the frame declaring it.
typedef struct mrhiDriverResource
{
    mrhiDriverResourceKind kind;
    bool needed;
    bool sealed;
    uint64_t handle;
    uint64_t image;
    const mrhiTextureDef* texture;
    uint64_t size;
    uint32_t usage;
    uint64_t memoryOffset;
    uint64_t memoryBytes;
} mrhiDriverResource;

// A resource a kept pass declares, its targets among them: its frame
// slot plus one, and the state the use leaves it in.
typedef struct mrhiDriverAccess
{
    uint32_t resource;
    mrhiResourceState state;
} mrhiDriverAccess;

// A kept pass of a submitted frame: its id, which its barriers name; its
// class and label (labelLength bytes of UTF-8 without NUL); its targets,
// naming frame resources by slot plus one, with the stores the compile
// derived; its render area; its occlusion query set's handle (0 for
// none); its timestamp query set's handle and the queries written at
// its start and end (MRHI_NO_QUERY for none); its heap's handle (0 for
// none); the resources it declares, which a heap may reach unbound; and
// its first command chunk, an index into the frame's chunks plus one (0
// for none), each chunk naming the next.
typedef struct mrhiDriverPass
{
    mrhiPassId id;
    mrhiPassClass passClass;
    const char* label;
    size_t labelLength;
    mrhiColorTarget colorTargets[MRHI_COLOR_TARGETS];
    mrhiStoreOp colorStores[MRHI_COLOR_TARGETS];
    uint32_t colorTargetCount;
    mrhiDepthTarget depthTarget;
    mrhiStoreOp depthStore;
    mrhiStoreOp stencilStore;
    uint32_t width;
    uint32_t height;
    // The views it renders (mrhi-0020), each into the next layer of every
    // target: 1 without multiview.
    uint32_t viewCount;
    uint64_t occlusionSet;
    uint64_t timestampSet;
    uint32_t timestampBegin;
    uint32_t timestampEnd;
    uint64_t heap;
    const mrhiDriverAccess* accesses;
    uint32_t accessCount;
    uint32_t firstChunk;
    // A native pass's command buffer, which the driver runs in its place
    // instead of a command stream; 0 for any other pass, and for a native
    // pass the program gave none (NULL).
    bool native;
    void* nativeCommands;
} mrhiDriverPass;

// A heap entry as its driver writes it (mrhi-0015): its kind, the
// view's or buffer's driver handle, a buffer's range with its size
// resolved, and whether shaders may write it.
typedef struct mrhiDriverHeapEntry
{
    mrhiHeapEntryKind kind;
    bool writable;
    uint64_t handle;
    uint64_t offset;
    uint64_t size;
} mrhiDriverHeapEntry;

// A submitted frame as the driver sees it (mrhi-0013): its
// resources by frame slot less one; its kept passes in the order they
// run; its barriers in the order they run, each before a pass or, with
// a null pass id, at the frame's end; its command chunks; its upload
// bytes, which copies from staging read at the offsets they name, valid
// until the frame finishes; the readback ring, which the driver fills
// at the offsets readbacks name before it reports the frame finished;
// and the bytes its transients take together. Everything else is valid
// during the call.
typedef struct mrhiDriverFrame
{
    const mrhiDriverResource* resources;
    uint32_t resourceCount;
    const mrhiDriverPass* passes;
    uint32_t passCount;
    const mrhiBarrier* barriers;
    size_t barrierCount;
    const mrhiCommandChunk* chunks;
    uint32_t chunkCount;
    const uint8_t* staging;
    uint64_t stagingBytes;
    uint8_t* readbackRing;
    uint64_t readbackBytes;
    uint64_t memoryBytes;
} mrhiDriverFrame;

// The driver side of a device: its vtable and pointer. A device driver
// is made at once and opens in the background; its instance driver
// answers the opening through a poll event. A def's label is valid
// (label.h) and read only during the call that passes it. Command
// streams name device objects by driver handle, and an object destroyed
// while a frame records may still be named by that frame's streams: a
// driver retires a destroyed object only once the next frame submitted
// after the call has finished, or when the device is destroyed.
typedef struct mrhiDeviceDriverVtable
{
    uint32_t spiVersion;
    uint32_t size;
    // First in every SPI version, so that the core can destroy a device
    // whose vtable fails the handshake.
    void (*destroy)(void* self);
    // Makes a sampler the core has checked; its handle, never zero.
    mrhiResult (*createSampler)(void* self, const mrhiSamplerDef* def, uint64_t* handleOut);
    void (*destroySampler)(void* self, uint64_t handle);
    // Makes a buffer the core has checked; its handle, never zero.
    mrhiResult (*createBuffer)(void* self, const mrhiBufferDef* def, uint64_t* handleOut);
    void (*destroyBuffer)(void* self, uint64_t handle);
    // Makes a texture the core has checked; its handle, never zero.
    mrhiResult (*createTexture)(void* self, const mrhiTextureDef* def, uint64_t* handleOut);
    // Destroys a texture whose views the core has destroyed.
    void (*destroyTexture)(void* self, uint64_t handle);
    // Makes a view of a texture from a def the core has checked and
    // resolved: its format, usage and counts filled in.
    mrhiResult (*createView)(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut);
    void (*destroyView)(void* self, uint64_t handle);
    // Configures a surface, by its instance driver's handle, from a
    // config the core has checked; its swapchain handle, never zero.
    // oldSwapchain, 0 for none, is retired whether or not this succeeds.
    mrhiResult (*configureSurface)(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut);
    void (*unconfigureSurface)(void* self, uint64_t swapchain);
    // Makes a shader from a container the core has checked, labeled by
    // the def; its handle, never zero. The container's bytes are only
    // read during the call. The core destroys every shader before its
    // device.
    mrhiResult (*createShader)(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut);
    void (*destroyShader)(void* self, uint64_t handle);
    // Starts a compute pipeline, answered by a poll event with the tag; its
    // handle, never zero, at once. An immediate failure is returned
    // instead. Everything it is given is only read during the call.
    mrhiResult (*createComputePipeline)(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut);
    // Starts a graphics pipeline as createComputePipeline starts a compute
    // one.
    mrhiResult (*createGraphicsPipeline)(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut);
    // Destroys a pipeline, pending or not; a pending one is never
    // reported.
    void (*destroyPipeline)(void* self, uint64_t handle);
    // Fills what the driver knows of its device's loss: the reason, the
    // faulting frame's tag and pass index when the API tells, and a
    // message. Called once, when the core loses the device.
    void (*lossReport)(void* self, mrhiDeviceLossReport* reportOut);
    // Acquires a swapchain's next image: mrhi_success or mrhi_suboptimal
    // with the image, never zero; mrhi_occluded or mrhi_errorOutOfDate
    // without one; or mrhi_errorDeviceLost.
    mrhiResult (*acquireImage)(void* self, uint64_t swapchain, uint64_t* imageOut);
    // Takes back an image a frame acquired and will not present, to hand
    // out again; its swapchain may have been unconfigured since.
    void (*releaseImage)(void* self, uint64_t swapchain, uint64_t image);
    // Makes a query set the core has checked; its handle, never zero.
    mrhiResult (*createQuerySet)(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut);
    void (*destroyQuerySet)(void* self, uint64_t handle);
    // The nanoseconds per timestamp tick of a device granted timestamps.
    double (*timestampPeriod)(void* self);
    // Takes a pipeline cache blob it exported on an earlier run, right
    // after the device is made; false when it declines it. The bytes are
    // only read during the call.
    bool (*importPipelineCache)(void* self, const void* bytes, size_t size);
    // Writes its pipeline cache blob when capacity allows and returns its
    // size either way.
    size_t (*exportPipelineCache)(void* self, void* bytes, size_t capacity);
    // The bytes a declared texture with its derived usages takes, and
    // their alignment, a power of two; 0 bytes when the GPU keeps it on
    // chip.
    void (*textureMemory)(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut);
    // The bytes a declared buffer with its derived usages takes, and their
    // alignment, a power of two.
    void (*bufferMemory)(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut);
    // Runs a frame on the GPU; finished, it is reported by poll with the
    // tag.
    mrhiResult (*submitFrame)(void* self, const mrhiDriverFrame* frame, uint64_t tag);
    // Moves up to capacity finished frames and pipelines into events and
    // returns how many it moved.
    size_t (*poll)(void* self, mrhiDriverEvent* events, size_t capacity);
    // Waits up to timeoutNs for a frame and returns whether it finished;
    // a finished frame is still reported by poll.
    bool (*waitFrame)(void* self, uint64_t tag, uint64_t timeoutNs);
    // Makes a heap the core has checked, every entry empty; its handle,
    // never zero. Its destruction waits for the frames that used it, as
    // other objects' do.
    mrhiResult (*createHeap)(void* self, const mrhiHeapDef* def, uint64_t* handleOut);
    void (*destroyHeap)(void* self, uint64_t handle);
    // Writes an entry the core has checked and found empty, which no
    // running frame reads.
    void (*writeHeapEntry)(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry);
    void (*writeHeapSampler)(void* self, uint64_t heap, uint32_t index, uint64_t sampler);
} mrhiDeviceDriverVtable;

typedef struct mrhiDeviceDriver
{
    const mrhiDeviceDriverVtable* vtable;
    void* self;
} mrhiDeviceDriver;

typedef struct mrhiInstanceDriverVtable
{
    uint32_t spiVersion;
    uint32_t size;
    // Starts a search for adapters, answered by an event with the tag.
    mrhiResult (*requestAdapters)(void* self, uint64_t tag);
    // Moves up to capacity finished requests into events and returns
    // how many it moved.
    size_t (*poll)(void* self, mrhiDriverEvent* events, size_t capacity);
    // Copies up to capacity adapters the last finished search found and
    // returns how many it found.
    size_t (*getAdapters)(const void* self, mrhiDriverAdapter* adapters, size_t capacity);
    // Fills what a format can do on an adapter.
    void (*getFormatCaps)(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut);
    // Makes a surface from the one source chained on a def the core has
    // checked; its handle, never zero. mrhi_errorUnsupported for a
    // source the driver cannot use.
    mrhiResult (*createSurface)(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut);
    void (*destroySurface)(void* self, uint64_t handle);
    // Fills what a surface can do on an adapter, the floors included
    // when the adapter presents there.
    void (*getSurfaceCaps)(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut);
    // Makes a device on an adapter from a def the core has checked (its
    // features and limits are the grant, its label read only during the
    // call), opening it in the background: the open is answered by an
    // event with the tag. An immediate failure is returned instead.
    mrhiResult (*createDevice)(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut);
    void (*destroy)(void* self);
} mrhiInstanceDriverVtable;

// A driver as an instance holds it.
typedef struct mrhiInstanceDriver
{
    const mrhiInstanceDriverVtable* vtable;
    void* self;
} mrhiInstanceDriver;

// The handshake (mrhi-0024): success for a vtable of this SPI version,
// at least the size the core knows, with every function; otherwise
// mrhi_errorVersion for another version, or mrhi_errorInvalid. A
// driver's own functions are not checked further.
mrhiResult mrhiCheckInstanceVtable(const mrhiInstanceDriverVtable* vtable);
mrhiResult mrhiCheckDeviceVtable(const mrhiDeviceDriverVtable* vtable);

#endif // MAUL_RHI_SRC_DRIVER_H
