// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device as the core sees it (mrhi-0004): its instance, what it was
// granted, its state and its driver.

#ifndef MAUL_RHI_SRC_DEVICE_CORE_H
#define MAUL_RHI_SRC_DEVICE_CORE_H

#include "capabilities_core.h"
#include "command.h"
#include "driver.h"
#include "pool.h"
#include "reflection.h"

#include "maul-rhi/device.h"

#include <stdatomic.h>

// Where a device texture or buffer was imported: the frame's serial and
// the frame's slot for it.
typedef struct mrhiImport
{
    uint32_t frame;
    uint32_t resource;
} mrhiImport;

// Where a readback stands: free, recorded in a frame, answered, or
// taken and waiting for the ring to pass it.
typedef enum mrhiReadbackState
{
    mrhiReadbackFree,
    mrhiReadbackRecorded,
    mrhiReadbackAnswered,
    mrhiReadbackTaken,
} mrhiReadbackState;

// A readback as its device keeps it: its request, state and answer; its
// bytes in the ring (their position counted from the ring's start, so
// that positions only grow, and the bytes they take); and the bytes the
// program takes: a texture's rows of rowBytes at a pitch, rows to a
// layer, or a buffer's bytes at pitch 0.
typedef struct mrhiReadback
{
    uint32_t request;
    mrhiReadbackState state;
    mrhiResult outcome;
    uint32_t pitch;
    uint32_t rowBytes;
    uint32_t rows;
    uint64_t position;
    uint64_t bytes;
    uint64_t size;
} mrhiReadback;

// A sampler as its device keeps it: its driver handle, and what binding
// it takes.
typedef struct mrhiSamplerSlot
{
    uint64_t handle;
    // Whether it compares, and whether any of its filters is linear.
    bool comparison;
    bool filtering;
    // The heap entries naming it, which its destruction empties.
    uint32_t heapRefs;
} mrhiSamplerSlot;

// A heap entry as its heap keeps it (mrhi-0015): the view, buffer or
// sampler slot it names and that slot's generation, 0 for an empty
// entry; a buffer's range; its kind and whether it is writable; and the
// token of the last frame submitted when it was emptied, 0 when no
// frame can still read it.
typedef struct mrhiHeapEntrySlot
{
    uint32_t object;
    uint32_t generation;
    uint64_t offset;
    uint64_t size;
    uint32_t freeAfter;
    mrhiHeapEntryKind kind;
    bool writable;
} mrhiHeapEntrySlot;

// A heap as its device keeps it: its driver handle, and its resource and
// sampler entries in one block from the device's allocator; entries 0
// for a free slot.
typedef struct mrhiHeapSlot
{
    uint64_t handle;
    uint32_t entries;
    uint32_t samplers;
    mrhiHeapEntrySlot* table;
} mrhiHeapSlot;

// A query set as its device keeps it: its driver handle, its type, and
// the run of the device's query marks it holds; a count of 0 for a free
// slot.
typedef struct mrhiQuerySetSlot
{
    uint64_t handle;
    uint32_t first;
    uint32_t count;
    mrhiQueryType type;
} mrhiQuerySetSlot;

// A buffer as its device keeps it.
typedef struct mrhiBufferSlot
{
    uint64_t handle;
    uint64_t size;
    mrhiBufferUsage usage;
    mrhiImport import;
    // The state frames leave it in.
    mrhiResourceState state;
    // The heap entries naming it, which its destruction empties.
    uint32_t heapRefs;
} mrhiBufferSlot;

// A texture as its device keeps it: its def (without its chain), its
// driver handle, and the slot of its newest view, 0 for none.
typedef struct mrhiTextureSlot
{
    uint64_t handle;
    mrhiTextureDef def;
    uint32_t firstView;
    mrhiImport import;
    // The state frames leave it in.
    mrhiResourceState state;
} mrhiTextureSlot;

// A view as its device keeps it: its resolved def (without its chain),
// its driver handle, its texture's slot, and the slots of its texture's
// views before and after it, 0 for none.
typedef struct mrhiViewSlot
{
    uint64_t handle;
    mrhiViewDef def;
    uint32_t texture;
    uint32_t previous;
    uint32_t next;
    // The heap entries naming it, which its destruction empties.
    uint32_t heapRefs;
} mrhiViewSlot;

// What a frame resource is.
typedef enum mrhiFrameResourceKind
{
    mrhiFrameTexture,
    mrhiFrameBuffer,
    mrhiImportedTexture,
    mrhiImportedBuffer,
    // An image acquired from a surface, presented at submission.
    mrhiSurfaceImage,
} mrhiFrameResourceKind;

// A resource of the open frame: a texture's def or a buffer's size
// (without their chains and labels); for an imported device texture or
// buffer, its id, and its driver handle and the state it begins the
// frame in, all as they were when it was imported, since it may be
// destroyed and its slot taken again while the frame is open.
typedef struct mrhiFrameResource
{
    mrhiFrameResourceKind kind;
    mrhiTextureDef texture;
    uint64_t size;
    uint64_t handle;
    uint32_t index1;
    uint32_t generation;
    mrhiResourceState initialState;
    // Whether it began the frame sealed and was not unsealed, which allows
    // only the reads of the sealed state, and whether it ends sealed.
    bool sealed;
    bool seal;
    // A surface image's driver image; its handle is its swapchain's.
    uint64_t image;
    // Whether a pass declared so far writes it; imports count as written.
    bool written;
    // Whether a kept pass needs it, and the usages kept passes make of
    // it, found by the compile, with the plan's transience, its first
    // and last kept passes (0 for none), the state its first use puts
    // it in and the state it ends in.
    bool needed;
    uint32_t usage;
    bool transient;
    uint32_t firstPass;
    uint32_t lastPass;
    mrhiResourceState firstState;
    mrhiResourceState finalState;
    // Where the compile placed a declared resource in the frame's memory.
    bool placed;
    uint64_t memoryOffset;
    uint64_t memoryBytes;
} mrhiFrameResource;

// A part of a resource in one state, in the compile's map of it.
typedef struct mrhiBox
{
    uint32_t baseMip;
    uint32_t mipCount;
    uint32_t baseLayer;
    uint32_t layerCount;
    uint8_t planes;
    mrhiResourceState state;
} mrhiBox;

// The uses a pass makes of a resource beyond the access kinds.
enum
{
    mrhiUseColorTarget = 16,
    mrhiUseResolve = 17,
    mrhiUseDepthTarget = 18,
};

// A use a pass makes of a resource: its slot in the frame, the access
// kind or target use, whether it reads or writes what is there, and the
// part of a texture it covers.
typedef struct mrhiFrameUse
{
    uint32_t resource;
    uint8_t use;
    // The state it leaves its part in, and the planes of that part: 1
    // for color or depth, 2 for stencil.
    mrhiResourceState state;
    uint8_t planes;
    bool reads;
    bool writes;
    uint32_t baseMip;
    uint32_t mipCount;
    uint32_t baseLayer;
    uint32_t layerCount;
} mrhiFrameUse;

// The binding tables a container has.
#define MRHI_TABLES 4

// What a render pass's targets are, and what a graphics pipeline expects
// of them: the color formats by location (mrhi_formatNone past the
// count), the depth or stencil format, the sample count, and whether
// depth or stencil is written.
typedef struct mrhiRenderLayout
{
    mrhiFormat colors[MRHI_COLOR_TARGETS];
    mrhiFormat depth;
    uint32_t samples;
    bool writesDepth;
    bool writesStencil;
} mrhiRenderLayout;

// Where a pass's recording stands.
typedef enum mrhiRecording
{
    mrhiRecordingIdle,
    mrhiRecordingOpen,
    mrhiRecordingEnded,
} mrhiRecording;

// A pass of the open frame: its class, its uses in the frame's use
// table, its targets as declared, and whether the compile kept it.
typedef struct mrhiFramePass
{
    mrhiPassClass passClass;
    bool neverCull;
    bool kept;
    uint32_t firstUse;
    uint32_t useCount;
    mrhiColorTarget colorTargets[MRHI_COLOR_TARGETS];
    uint32_t colorTargetCount;
    mrhiDepthTarget depthTarget;
    // The stores the compile derived for its targets.
    mrhiStoreOp colorStores[MRHI_COLOR_TARGETS];
    mrhiStoreOp depthStore;
    mrhiStoreOp stencilStore;
    // Its recording: an mrhiRecording, claimed atomically by the thread
    // that begins it; its chunks (indices plus one, 0 for none); its open
    // debug groups; whether a command found the arena full; for a render
    // pass, its targets' layout and size; the pipeline last set (a slot
    // plus one and its generation, 0 for none); the tables set, by bit,
    // with the digests of the containers they were set under; and the
    // index buffer's format (none when unset) and bytes. Its vertex
    // buffers' bytes are in the device's frameVertexBytes.
    _Atomic uint32_t recording;
    uint32_t firstChunk;
    uint32_t lastChunk;
    uint32_t debugDepth;
    bool overflowed;
    mrhiRenderLayout layout;
    uint32_t width;
    uint32_t height;
    uint32_t pipeline;
    uint32_t pipelineGeneration;
    uint8_t tablesSet;
    uint8_t tableDigests[MRHI_TABLES][MRHI_DIGEST_BYTES];
    mrhiIndexFormat indexFormat;
    uint64_t indexBytes;
    // Its occlusion query set (a slot plus one and its generation, 0 for
    // none), and whether one of its queries is open.
    uint32_t occlusionSet;
    uint32_t occlusionGeneration;
    uint64_t occlusionHandle;
    bool occlusionOpen;
    // Its timestamp query set's driver handle (0 for none) and the
    // queries written at its start and end (MRHI_NO_QUERY for none).
    uint64_t timestampSet;
    uint32_t timestampBegin;
    uint32_t timestampEnd;
    // Its heap's driver handle, 0 for none.
    uint64_t heap;
    // Its label's bytes, in the device's frameLabels.
    uint32_t labelLength;
} mrhiFramePass;

// A surface as the device that configured it keeps it: the surface, the
// driver's swapchain handle, the configuration (without its chain), and
// the last frame that acquired its image, with the answer and the
// frame's slot for the image (0 for none).
typedef struct mrhiSwapchainSlot
{
    mrhiSurfaceId surface;
    uint64_t handle;
    mrhiSurfaceConfig config;
    mrhiImport acquired;
    mrhiResult acquireOutcome;
} mrhiSwapchainSlot;

// A shader container as its device keeps it: its driver handle and its
// reflection, NULL for a free slot.
typedef struct mrhiShaderSlot
{
    uint64_t handle;
    mrhiReflection* reflection;
} mrhiShaderSlot;

// What a pipeline is; none for a free slot.
typedef enum mrhiPipelineKind
{
    mrhiPipelineNone,
    mrhiPipelineCompute,
    mrhiPipelineGraphics,
} mrhiPipelineKind;

// Where a pipeline's creation stands.
typedef enum mrhiPipelineState
{
    mrhiPipelinePending,
    mrhiPipelineReady,
    mrhiPipelineFailed,
} mrhiPipelineState;

// A pipeline as its device keeps it: its kind and state, the request
// its creation answers, its driver handle, its shader's reflection
// (NULL for a free slot) and its entry points there.
typedef struct mrhiPipelineSlot
{
    mrhiPipelineKind kind;
    mrhiPipelineState state;
    uint32_t request;
    uint64_t handle;
    mrhiReflection* reflection;
    uint32_t entries[2];
    // What its entry points read through heaps, so that it is set only in
    // passes naming one.
    mrhiShaderHeapUses heapUses;
    // A graphics pipeline's targets, its vertex buffers (their facts in
    // the device's pipelineVertex), and its strip index format.
    mrhiRenderLayout layout;
    uint32_t vertexBufferCount;
    mrhiIndexFormat stripIndexFormat;
} mrhiPipelineSlot;

// What a draw needs of one of a graphics pipeline's vertex buffers: its
// stride, the bytes its attributes reach in an element, and its step.
typedef struct mrhiVertexFacts
{
    uint32_t stride;
    uint32_t lastStride;
    mrhiVertexStepMode stepMode;
} mrhiVertexFacts;

struct mrhiDevice
{
    mrhiInstance* instance;
    // The driver handle of the adapter it was opened on.
    uint64_t adapter;
    mrhiAllocator allocator;
    mrhiFeatures features;
    mrhiLimits limits;
    mrhiDeviceLimits deviceLimits;
    mrhiDeviceState state;
    // The open request, answered when the device leaves opening.
    uint32_t request;
    // Calls refused as invalid input, counted from any recording thread.
    _Atomic uint64_t misuse;
    mrhiDeviceDriver driver;
    // Who made the device, for its pipeline cache's envelope, and what
    // became of the cache its def gave.
    mrhiAdapterInfo adapterInfo;
    mrhiResult cacheOutcome;
    // The block the device and its tables live in.
    size_t bytes;
    // Samplers: ids, and each slot's driver handle.
    mrhiPool samplers;
    mrhiSamplerSlot* samplerSlots;
    // Buffers: ids, and each slot's driver handle and def.
    mrhiPool buffers;
    mrhiBufferSlot* bufferSlots;
    mrhiPool textures;
    mrhiTextureSlot* textureSlots;
    mrhiPool views;
    mrhiViewSlot* viewSlots;
    // Query sets: ids, each slot's handle and run, and a mark per query:
    // the serial of the frame that last wrote it.
    mrhiPool querySets;
    mrhiQuerySetSlot* querySetSlots;
    _Atomic uint64_t* queryMarks;
    // Bindless heaps.
    mrhiPool heaps;
    mrhiHeapSlot* heapSlots;
    // The surfaces it configured.
    mrhiPool swapchains;
    mrhiSwapchainSlot* swapchainSlots;
    mrhiPool shaders;
    mrhiShaderSlot* shaderSlots;
    mrhiPool pipelines;
    mrhiPipelineSlot* pipelineSlots;
    // Each graphics pipeline's vertex buffers, vertexBuffers per slot.
    mrhiVertexFacts* pipelineVertex;
    // Pipelines whose creation the driver has not answered; each has room
    // for its answer in the queue.
    uint32_t pendingCount;
    // Frames: whether one is open and its serial, never 0, its resources,
    // the last token given, and the tokens of the frames the GPU has not
    // finished, at most framesInFlight.
    bool frameOpen;
    bool frameCompiled;
    uint32_t frameSerial;
    // Frames begun, never 0 once one is: what query marks hold.
    uint64_t frameNumber;
    mrhiFrameResource* frameResources;
    uint32_t frameResourceCount;
    mrhiFramePass* framePasses;
    uint32_t framePassCount;
    mrhiFrameUse* frameUses;
    uint32_t frameUseCount;
    // The compile's plan: the barriers in the order they run, and room to
    // sort them and to map one resource's states.
    mrhiBarrier* frameBarriers;
    mrhiBarrier* frameBarrierScratch;
    uint32_t frameBarrierCount;
    uint32_t* frameCounts;
    mrhiBox* frameBoxes;
    uint32_t frameBoxLimit;
    // The bytes the placed resources take together, and room to order
    // the placed resources a new one meets.
    uint64_t frameMemory;
    uint32_t* frameOrder;
    // The frame's command arena: its chunks, and those taken so far.
    // Readbacks: their records and ring, both used in order and taken
    // under the lock, since passes record in parallel; the records taken
    // and freed (counts that only grow), the ring's positions likewise,
    // the readbacks not yet answered, the open frame's first record and
    // position, and each running frame's first record and count.
    mrhiReadback* readbacks;
    uint8_t* readbackRing;
    atomic_flag readbackLock;
    uint32_t readbackHead;
    uint32_t readbackTail;
    uint64_t ringHead;
    uint64_t ringTail;
    uint32_t readbackPending;
    uint32_t frameReadbackFirst;
    uint64_t frameRingFirst;
    uint32_t* runningReadbackFirst;
    uint32_t* runningReadbackCount;
    // The frames' staging: framesInFlight regions of frameUploadBytes,
    // the open frame's region, the bytes taken of it, and the region of
    // each running frame, beside its token in running.
    uint8_t* frameStaging;
    uint32_t stagingRegion;
    _Atomic uint64_t stagingTaken;
    uint32_t* runningRegions;
    // Each pass's vertex buffers' bytes plus one (0 for unset),
    // vertexBuffers per pass.
    uint64_t* frameVertexBytes;
    mrhiCommandChunk* frameChunks;
    uint32_t frameChunkCount;
    _Atomic uint32_t frameChunksTaken;
    // Each pass's label, MRHI_LABEL_BYTES apiece; and the tables a
    // submitted frame's view is built in.
    char* frameLabels;
    mrhiDriverPass* driverPasses;
    mrhiDriverResource* driverResources;
    mrhiDriverAccess* driverAccesses;
    // The last request given, frames' tokens and pipelines' requests
    // alike.
    uint32_t lastRequest;
    uint32_t* running;
    uint32_t runningCount;
    // The tokens of the last frame submitted and the last finished, and
    // what is known of the device's loss once it is lost.
    uint32_t lastSubmitted;
    uint32_t lastFinished;
    mrhiDeviceLossReport lossReport;
    // A ring of deviceLimits.notifications records.
    mrhiDeviceNotification* queue;
    uint32_t queueHead;
    uint32_t queueCount;
    // What each known format can do on this device: the adapter's
    // capabilities, with the compressed families the device was not
    // granted cleared.
    mrhiFormatCaps formatCaps[MRHI_KNOWN_FORMATS];
};

// mrhi_success for a ready device, mrhi_errorState for one that is not.
mrhiResult mrhiDeviceUsable(const mrhiDevice* device);

// Counts one misuse on the device and returns mrhi_errorInvalid.
mrhiResult mrhiDeviceMisuse(mrhiDevice* device);

// The head every object def opens with.
typedef struct mrhiDefHead
{
    uint32_t cookie;
    const mrhiChain* next;
    const char* label;
    size_t labelLength;
} mrhiDefHead;

// The head of a def pointer.
#define MRHI_DEF_HEAD(def)                                                                         \
    ((mrhiDefHead){(def)->cookie, (def)->next, (def)->label, (def)->labelLength})

// Checks an object def's cookie, extension chain and label on a live
// device: success, or the refusal (invalid input counted as misuse).
mrhiResult mrhiCheckObjectDef(mrhiDevice* device, mrhiDefHead head, uint32_t expected);

// Checks what a texture def says of its shape on a live device (its
// cookie, chain and label, format, size, layers, mips, samples and view
// formats): success, or the refusal, invalid input counted as misuse.
mrhiResult mrhiCheckTextureShape(mrhiDevice* device, const mrhiTextureDef* def);

// Checks what a buffer def says of its shape on a live device (its
// cookie, chain and label, and size): success, or the refusal.
mrhiResult mrhiCheckBufferShape(mrhiDevice* device, const mrhiBufferDef* def);

// Checks a texture def's usages, and that its format takes them and its
// sample count on the device: success, or the refusal.
mrhiResult mrhiCheckTextureUsage(mrhiDevice* device, const mrhiTextureDef* def);

// Whether a format has an aspect; every format has mrhi_aspectAll.
bool mrhiFormatHasAspect(mrhiFormat format, mrhiTextureAspect aspect);

// A count, with MRHI_REMAINING resolved to what follows the base.
uint32_t mrhiResolveCount(uint32_t count, uint32_t base, uint32_t total);

// Whether a range of at least one fits in the total.
bool mrhiIsRangeValid(uint32_t base, uint32_t count, uint32_t total);

// The def of a texture resource: its declaration, or the imported
// texture's.
const mrhiTextureDef* mrhiFrameTextureOf(const mrhiFrameResource* resource);

// Finds a resource of the open frame whose imported object still lives:
// its slot, or 0.
uint32_t mrhiFindFrameResource(const mrhiDevice* device, mrhiResourceId id);

// Resolves a view def against its texture into resolvedOut (its format,
// usage and counts made explicit) and returns whether it is valid there.
bool mrhiResolveView(const mrhiTextureDef* texture, const mrhiViewDef* def,
                     mrhiViewDef* resolvedOut);

// Whether a frame resource is an imported device object.
bool mrhiIsImported(const mrhiFrameResource* resource);

// Whether what the frame writes to a resource is seen after it: an
// imported object's or a surface image's.
bool mrhiOutlivesFrame(const mrhiFrameResource* resource);

// Gives the driver back the images the open frame acquired, for a frame
// dropped or whose submission failed.
void mrhiReleaseImages(mrhiDevice* device);

// The usage bit of a texture or buffer a use needs.
uint32_t mrhiUsageOf(const mrhiFrameResource* resource, uint8_t use);

// Whether a state writes what is there.
bool mrhiStateWrites(mrhiResourceState state);

// The planes of a format: 3 with stencil, else 1.
uint8_t mrhiFormatPlanes(mrhiFormat format);

// Plans the kept passes' barriers and each resource's lifetime,
// transience and final state: success, or mrhi_errorCapacity.
mrhiResult mrhiPlan(mrhiDevice* device);

// Derives the stores of the kept passes' targets.
void mrhiDeriveStores(mrhiDevice* device);

// Places the declared resources the kept passes use in the frame's
// memory by lifetime: success, or mrhi_errorCapacity when the offsets
// overflow.
mrhiResult mrhiPlace(mrhiDevice* device);

// Marks the first use of each placed resource that takes memory used
// earlier in the frame, adding a buffer's barrier for it: success, or
// mrhi_errorCapacity when the barriers run out.
mrhiResult mrhiPlanAliasing(mrhiDevice* device);

// Leaves each imported object in the state the submitted frame left it.
void mrhiApplyFinalStates(mrhiDevice* device);

// Compiles the open frame: culls, derives usages and checks them.
mrhiResult mrhiCompile(mrhiDevice* device);

// Whether a format can take the usages on the device.
bool mrhiFormatTakes(const mrhiDevice* device, mrhiFormat format, mrhiTextureUsage usage);

// Destroys a texture's views and ends their ids.
void mrhiDestroyViewsOf(mrhiDevice* device, mrhiTextureSlot* texture);

// Ends the configuration in a live swapchain slot: the driver's
// swapchain, the surface's record of it, and the slot.
void mrhiEndConfiguration(mrhiDevice* device, uint32_t swapchain);

// Ends every configuration the device holds.
void mrhiEndConfigurations(mrhiDevice* device);

// Destroys every shader the device holds, in its driver and its tables.
void mrhiDestroyShaders(mrhiDevice* device);

// Checks the pipeline cache a def gives and hands its blob to the
// driver: what became of it, for mrhiGetPipelineCacheOutcome.
mrhiResult mrhiImportPipelineCache(mrhiDevice* device, const void* bytes, size_t size);

// Destroys every pipeline the device holds, answering none.
void mrhiDestroyPipelines(mrhiDevice* device);

// Whether the queue has room for one more answer beside those it holds
// and those running frames, pending pipelines and readbacks will give.
bool mrhiHasAnswerRoom(const mrhiDevice* device);

// The answers the queue still has room for.
uint32_t mrhiAnswerRoom(const mrhiDevice* device);

// Readbacks as frames begin, are dropped and finish: the open frame's
// first record and position marked; its readbacks rolled back; a
// finished frame's readbacks answered with its outcome.
void mrhiMarkReadbacks(mrhiDevice* device);
void mrhiDropReadbacks(mrhiDevice* device);
void mrhiAnswerReadbacks(mrhiDevice* device, uint32_t first, uint32_t count, mrhiResult outcome);

// Queues an answer the device made room for.
void mrhiQueueAnswer(mrhiDevice* device, mrhiDeviceNotificationKind kind, uint32_t request,
                     mrhiResult outcome);

// Answers a pending pipeline the driver finished, by its tag: its slot
// above its request.
void mrhiFinishPipeline(mrhiDevice* device, uint64_t tag, mrhiResult outcome);

// Answers every pending pipeline of a device being lost.
void mrhiLosePipelines(mrhiDevice* device);

// Loses a device, once (mrhi-0014): its state, the driver's report, then
// a loss notice and every answer still owed, each mrhi_errorDeviceLost.
void mrhiLoseDevice(mrhiDevice* device);

// Passes a driver call's status through, losing the device when it is
// mrhi_errorDeviceLost.
mrhiResult mrhiDriverStatus(mrhiDevice* device, mrhiResult status);

// Checks the query sets a pass def names: success; mrhi_errorStale for
// one the device no longer has; or mrhi_errorInvalid for an occlusion
// set that is not one or is named by a pass without targets, or
// timestamps that are not a graphics pass's two different queries, at
// least one given, in range and not yet written this frame.
mrhiResult mrhiCheckPassQueries(const mrhiDevice* device, const mrhiPassDef* def);

// Marks a checked pass def's timestamp queries written this frame and
// keeps them in its pass.
void mrhiMarkPassTimestamps(mrhiDevice* device, const mrhiPassDef* def, mrhiFramePass* pass);

// Builds the view of the open, compiled frame a driver is submitted
// (mrhi-0013).
void mrhiViewFrame(mrhiDevice* device, mrhiDriverFrame* frameOut);

#endif // MAUL_RHI_SRC_DEVICE_CORE_H
