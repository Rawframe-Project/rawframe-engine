// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compile's plan (mrhi-0008): each resource's parts are mapped to the
// state their last use left them in, as disjoint boxes of mips, layers
// and planes, and each kept use makes barriers from the states it meets.
// Imported objects carry one state between frames, so the frame's end
// unifies theirs. The barriers are then put in the order they run. Once
// the declared resources are placed, the first use of one over memory
// used earlier in the frame is marked as aliasing, a buffer's gaining a
// barrier from the undefined state for it.

#include "device_core.h"

bool mrhiStateWrites(mrhiResourceState state)
{
    return state == mrhi_stateStorageWrite || state == mrhi_stateStorageReadWrite ||
           state == mrhi_stateCopyDestination || state == mrhi_stateColorTarget ||
           state == mrhi_stateResolve || state == mrhi_stateDepthTarget ||
           state == mrhi_stateQueryResolve;
}

// Whether going from one state to another needs a barrier: a texture
// leaving the undefined state, a write on either side, or a texture
// changing state.
static bool NeedsBarrier(mrhiResourceState before, mrhiResourceState after, bool texture)
{
    if (before == mrhi_stateUndefined)
    {
        return texture;
    }
    return mrhiStateWrites(before) || mrhiStateWrites(after) || (texture && before != after);
}

static bool IsTexture(const mrhiFrameResource* resource)
{
    return resource->kind == mrhiFrameTexture || resource->kind == mrhiImportedTexture ||
           resource->kind == mrhiSurfaceImage;
}

// The box a whole resource covers.
static mrhiBox WholeOf(const mrhiFrameResource* resource)
{
    mrhiBox box = {.mipCount = 1, .layerCount = 1, .planes = 1};
    const mrhiTextureDef* def = &resource->texture;
    if (IsTexture(resource))
    {
        box.mipCount = def->mipLevels;
        box.layerCount = def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
        box.planes = mrhiFormatPlanes(def->format);
    }
    return box;
}

// Where an imported object that is still live keeps the state frames
// leave it in: NULL for a declared resource or a destroyed object.
static mrhiResourceState* CarriedState(mrhiDevice* device, const mrhiFrameResource* resource)
{
    if (resource->kind == mrhiImportedTexture &&
        mrhiPoolIsLive(&device->textures, resource->index1, resource->generation))
    {
        return &device->textureSlots[resource->index1 - 1].state;
    }
    if (resource->kind == mrhiImportedBuffer &&
        mrhiPoolIsLive(&device->buffers, resource->index1, resource->generation))
    {
        return &device->bufferSlots[resource->index1 - 1].state;
    }
    return nullptr;
}

// The common part of two boxes: whether they meet, with it in out.
static bool Intersect(const mrhiBox* a, const mrhiBox* b, mrhiBox* out)
{
    uint32_t mipStart = a->baseMip > b->baseMip ? a->baseMip : b->baseMip;
    uint32_t mipEnd = a->baseMip + a->mipCount < b->baseMip + b->mipCount
                          ? a->baseMip + a->mipCount
                          : b->baseMip + b->mipCount;
    uint32_t layerStart = a->baseLayer > b->baseLayer ? a->baseLayer : b->baseLayer;
    uint32_t layerEnd = a->baseLayer + a->layerCount < b->baseLayer + b->layerCount
                            ? a->baseLayer + a->layerCount
                            : b->baseLayer + b->layerCount;
    uint8_t planes = a->planes & b->planes;
    *out =
        (mrhiBox){mipStart, mipEnd - mipStart, layerStart, layerEnd - layerStart, planes, a->state};
    return mipStart < mipEnd && layerStart < layerEnd && planes != 0;
}

// The texture range of a box, with its planes as an aspect.
static mrhiTextureRange RangeOf(const mrhiBox* box, uint8_t planes)
{
    mrhiTextureAspect aspect = mrhi_aspectAll;
    if (box->planes != planes)
    {
        aspect = box->planes == 1 ? mrhi_aspectDepthOnly : mrhi_aspectStencilOnly;
    }
    return (mrhiTextureRange){box->baseMip, box->mipCount, box->baseLayer, box->layerCount, aspect};
}

// The map of one resource during planning: its boxes in one half of the
// device's room while the next map is built in the other, the planes of
// the whole, and whether a limit was reached.
typedef struct Map
{
    mrhiDevice* device;
    uint32_t resource;
    uint8_t planes;
    bool texture;
    mrhiBox* boxes;
    mrhiBox* next;
    uint32_t count;
    uint32_t nextCount;
    uint32_t half;
    bool full;
} Map;

static void Push(Map* map, mrhiBox box)
{
    if (map->nextCount == map->half)
    {
        map->full = true;
        return;
    }
    map->next[map->nextCount++] = box;
}

static void Emit(Map* map, uint32_t pass, const mrhiBox* part, mrhiResourceState after)
{
    mrhiDevice* device = map->device;
    if (device->frameBarrierCount == device->deviceLimits.frameBarriers)
    {
        map->full = true;
        return;
    }
    uint32_t serial = device->frameSerial;
    device->frameBarriers[device->frameBarrierCount++] = (mrhiBarrier){
        .pass = {pass, pass == 0 ? 0 : serial},
        .resource = {map->resource, serial},
        .range = RangeOf(part, map->planes),
        .before = part->state,
        .after = after,
    };
}

// Adds the parts of a box outside another, in the box's state: before
// and after its mips, then before and after its layers within those
// mips, then its other planes within both.
static void PushRest(Map* map, const mrhiBox* box, const mrhiBox* cut, const mrhiBox* middle)
{
    uint32_t cutMipEnd = cut->baseMip + cut->mipCount;
    uint32_t boxMipEnd = box->baseMip + box->mipCount;
    uint32_t cutLayerEnd = cut->baseLayer + cut->layerCount;
    uint32_t boxLayerEnd = box->baseLayer + box->layerCount;
    mrhiBox part = *box;
    if (box->baseMip < cut->baseMip)
    {
        part.mipCount = cut->baseMip - box->baseMip;
        Push(map, part);
    }
    if (boxMipEnd > cutMipEnd)
    {
        part.baseMip = cutMipEnd;
        part.mipCount = boxMipEnd - cutMipEnd;
        Push(map, part);
    }
    part = *box;
    part.baseMip = middle->baseMip;
    part.mipCount = middle->mipCount;
    if (box->baseLayer < cut->baseLayer)
    {
        part.layerCount = cut->baseLayer - box->baseLayer;
        Push(map, part);
    }
    if (boxLayerEnd > cutLayerEnd)
    {
        part.baseLayer = cutLayerEnd;
        part.layerCount = boxLayerEnd - cutLayerEnd;
        Push(map, part);
    }
    part = *middle;
    part.planes = (uint8_t)(box->planes & ~cut->planes);
    if (part.planes != 0)
    {
        Push(map, part);
    }
}

// Makes the next map the current one.
static void Swap(Map* map)
{
    mrhiBox* boxes = map->boxes;
    map->boxes = map->next;
    map->next = boxes;
    map->count = map->nextCount;
    map->nextCount = 0;
}

// Moves a part of the map to a state, before a pass or at the frame's end
// (pass 0): barriers from every state it meets, at the end only from
// other states.
static void Move(Map* map, uint32_t pass, mrhiBox cut)
{
    for (uint32_t i = 0; i < map->count; ++i)
    {
        const mrhiBox* box = &map->boxes[i];
        mrhiBox part;
        if (!Intersect(box, &cut, &part))
        {
            Push(map, *box);
            continue;
        }
        bool needed =
            pass == 0 ? box->state != cut.state : NeedsBarrier(box->state, cut.state, map->texture);
        if (needed)
        {
            Emit(map, pass, &part, cut.state);
        }
        PushRest(map, box, &cut, &part);
    }
    Push(map, cut);
    Swap(map);
}

// Plans one resource over the kept passes, then its end: an imported
// object's parts all move to its last use's state.
static bool PlanResource(mrhiDevice* device, uint32_t slot)
{
    mrhiFrameResource* resource = &device->frameResources[slot - 1];
    mrhiBox whole = WholeOf(resource);
    // A declared resource begins undefined.
    whole.state = resource->initialState;
    uint32_t half = device->frameBoxLimit / 2;
    Map map = {
        .device = device,
        .resource = slot,
        .planes = whole.planes,
        .texture = IsTexture(resource),
        .boxes = device->frameBoxes,
        .next = device->frameBoxes + half,
        .count = 1,
        .half = half,
    };
    map.boxes[0] = whole;
    resource->firstPass = 0;
    resource->lastPass = 0;
    resource->finalState = whole.state;
    bool targetsOnly = true;
    for (uint32_t p = 0; p < device->framePassCount && !map.full; ++p)
    {
        const mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        for (uint32_t i = 0; i < pass->useCount && pass->kept; ++i)
        {
            if (uses[i].resource != slot)
            {
                continue;
            }
            resource->firstState = resource->firstPass == 0 ? uses[i].state : resource->firstState;
            resource->firstPass = resource->firstPass == 0 ? p + 1 : resource->firstPass;
            resource->lastPass = p + 1;
            resource->finalState = uses[i].state;
            bool target = uses[i].use == mrhiUseColorTarget || uses[i].use == mrhiUseDepthTarget;
            targetsOnly = targetsOnly && target && uses[i].writes && !uses[i].reads;
            Move(&map, p + 1,
                 (mrhiBox){uses[i].baseMip, uses[i].mipCount, uses[i].baseLayer, uses[i].layerCount,
                           uses[i].planes, uses[i].state});
        }
    }
    resource->transient =
        resource->kind == mrhiFrameTexture && resource->firstPass != 0 && targetsOnly;
    // A surface image ends ready to present, a sealed object sealed, and
    // an imported object otherwise in its last use's state.
    if (resource->kind == mrhiSurfaceImage)
    {
        resource->finalState = mrhi_statePresent;
    }
    if (resource->seal)
    {
        resource->finalState = mrhi_stateSealed;
    }
    if (mrhiOutlivesFrame(resource))
    {
        whole.state = resource->finalState;
        Move(&map, 0, whole);
    }
    return !map.full;
}

// Puts the barriers in the order they run: by pass, the frame's end last,
// each pass's in the order they were made.
static void Sort(mrhiDevice* device)
{
    uint32_t buckets = device->framePassCount + 2;
    uint32_t* counts = device->frameCounts;
    for (uint32_t i = 0; i < buckets; ++i)
    {
        counts[i] = 0;
    }
    const mrhiBarrier* barriers = device->frameBarriers;
    for (uint32_t i = 0; i < device->frameBarrierCount; ++i)
    {
        uint32_t pass = barriers[i].pass.index1;
        ++counts[pass == 0 ? buckets - 1 : pass];
    }
    uint32_t start = 0;
    for (uint32_t i = 0; i < buckets; ++i)
    {
        uint32_t count = counts[i];
        counts[i] = start;
        start += count;
    }
    for (uint32_t i = 0; i < device->frameBarrierCount; ++i)
    {
        uint32_t pass = barriers[i].pass.index1;
        device->frameBarrierScratch[counts[pass == 0 ? buckets - 1 : pass]++] = barriers[i];
    }
    for (uint32_t i = 0; i < device->frameBarrierCount; ++i)
    {
        device->frameBarriers[i] = device->frameBarrierScratch[i];
    }
}

mrhiResult mrhiPlan(mrhiDevice* device)
{
    device->frameBarrierCount = 0;
    for (uint32_t slot = 1; slot <= device->frameResourceCount; ++slot)
    {
        if (!PlanResource(device, slot))
        {
            return mrhi_errorCapacity;
        }
    }
    Sort(device);
    return mrhi_success;
}

// Whether a placed resource takes memory that another placed resource
// used in kept passes before its first.
static bool Aliases(const mrhiDevice* device, const mrhiFrameResource* resource)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* other = &device->frameResources[i];
        if (other->placed && other->lastPass < resource->firstPass &&
            other->memoryOffset < resource->memoryOffset + resource->memoryBytes &&
            resource->memoryOffset < other->memoryOffset + other->memoryBytes)
        {
            return true;
        }
    }
    return false;
}

mrhiResult mrhiPlanAliasing(mrhiDevice* device)
{
    uint32_t serial = device->frameSerial;
    uint32_t planned = device->frameBarrierCount;
    for (uint32_t slot = 1; slot <= device->frameResourceCount; ++slot)
    {
        const mrhiFrameResource* resource = &device->frameResources[slot - 1];
        if (!resource->placed || !Aliases(device, resource))
        {
            continue;
        }
        bool marked = false;
        for (uint32_t i = 0; i < planned; ++i)
        {
            mrhiBarrier* barrier = &device->frameBarriers[i];
            if (barrier->resource.index1 == slot && barrier->pass.index1 == resource->firstPass &&
                barrier->before == mrhi_stateUndefined)
            {
                barrier->aliasing = true;
                marked = true;
            }
        }
        // A texture's first use always has barriers from the undefined
        // state; a buffer's gets one.
        if (marked)
        {
            continue;
        }
        if (device->frameBarrierCount == device->deviceLimits.frameBarriers)
        {
            return mrhi_errorCapacity;
        }
        device->frameBarriers[device->frameBarrierCount++] = (mrhiBarrier){
            .pass = {resource->firstPass, serial},
            .resource = {slot, serial},
            .range = {0, 1, 0, 1, mrhi_aspectAll},
            .before = mrhi_stateUndefined,
            .after = resource->firstState,
            .aliasing = true,
        };
    }
    if (device->frameBarrierCount > planned)
    {
        Sort(device);
    }
    return mrhi_success;
}

void mrhiApplyFinalStates(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* resource = &device->frameResources[i];
        mrhiResourceState* carried = CarriedState(device, resource);
        if (carried != nullptr)
        {
            *carried = resource->finalState;
        }
    }
}

mrhiResult mrhiGetFrameBarriers(mrhiDevice* device, mrhiBarrier* barriers, size_t capacity,
                                size_t* countOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (countOut == nullptr || (barriers == nullptr && capacity > 0))
    {
        return mrhiDeviceMisuse(device);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    size_t count = device->frameBarrierCount;
    for (size_t i = 0; i < count && i < capacity; ++i)
    {
        barriers[i] = device->frameBarriers[i];
    }
    *countOut = count;
    return mrhi_success;
}

mrhiResult mrhiGetResourcePlan(mrhiDevice* device, mrhiResourceId resource,
                               mrhiResourcePlan* planOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (planOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (resource.generation != device->frameSerial || resource.index1 == 0 ||
        resource.index1 > device->frameResourceCount)
    {
        return mrhi_errorStale;
    }
    const mrhiFrameResource* planned = &device->frameResources[resource.index1 - 1];
    uint32_t serial = device->frameSerial;
    *planOut = (mrhiResourcePlan){
        .usage = planned->usage,
        .transient = planned->transient,
        .firstPass = {planned->firstPass, planned->firstPass == 0 ? 0 : serial},
        .lastPass = {planned->lastPass, planned->lastPass == 0 ? 0 : serial},
        .memoryOffset = planned->memoryOffset,
        .memoryBytes = planned->memoryBytes,
    };
    return mrhi_success;
}
