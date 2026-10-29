// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compile's plan: each resource's parts are mapped to the state their last use left them in, as
// disjoint boxes of mips, layers and planes, and each kept use makes barriers from the states it
// meets. Imported objects carry one state between frames, so the frame's end unifies theirs, an
// adopted image's to the state it rests in. The barriers are then put in the order they run. Once
// the declared resources are placed, the first use of one over memory used earlier in the frame is
// marked as aliasing, a buffer's gaining a barrier from the undefined state for it.

#include "device_core.h"
#include "invariant.h"

#include "generated/out_copies.h"

bool mrhiStateWrites(mrhiResourceState state)
{
    return state == mrhi_stateStorageWrite || state == mrhi_stateStorageReadWrite ||
           state == mrhi_stateCopyDestination || state == mrhi_stateColorTarget ||
           state == mrhi_stateResolve || state == mrhi_stateDepthTarget ||
           state == mrhi_stateQueryResolve;
}

// Whether going from one state to another needs a barrier: a texture
// leaving the undefined state, any change of state, or a write after a
// write. A buffer read in a second state after a first takes one too:
// the barrier into the first made the last write visible to that state's
// reads only, and a later write must wait for the reads of both.
static bool NeedsBarrier(mrhiResourceState before, mrhiResourceState after, bool texture)
{
    if (before == mrhi_stateUndefined)
    {
        return texture;
    }
    return before != after || mrhiStateWrites(after);
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

// The common part of two boxes: whether they meet, with it in out, in the
// first's state and tracking.
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
    *out = *a;
    out->baseMip = mipStart;
    out->mipCount = mipEnd - mipStart;
    out->baseLayer = layerStart;
    out->layerCount = layerEnd - layerStart;
    out->planes = a->planes & b->planes;
    return mipStart < mipEnd && layerStart < layerEnd && out->planes != 0;
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

// The part of a resource a use covers, in the state it leaves it in.
static mrhiBox BoxOf(const mrhiFrameResource* resource, const mrhiFrameUse* use)
{
    uint32_t baseLayer = 0;
    uint32_t layerCount = 0;
    mrhiTrackedLayers(resource, use, &baseLayer, &layerCount);
    return (mrhiBox){
        .baseMip = use->baseMip,
        .mipCount = use->mipCount,
        .baseLayer = baseLayer,
        .layerCount = layerCount,
        .planes = use->planes,
        .state = use->state,
    };
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
    // The code of the limit the map found full: 0 for none.
    mrhiDiagnosticCode full;
    // Whether the barriers a move makes are the library's.
    bool library;
    // The parts of the whole, which the boxes cover exactly once.
    uint64_t volume;
} Map;

static uint64_t Volume(const mrhiBox* box)
{
    return (uint64_t)box->mipCount * box->layerCount * (uint64_t)__builtin_popcount(box->planes);
}

// Whether the map's boxes still cover the whole exactly once: each move
// splits boxes and replaces a part, so their volumes always sum to the
// whole's, and none is empty. Only assertions call it.
[[maybe_unused]] static bool CoversWhole(const Map* map)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < map->count; ++i)
    {
        uint64_t volume = Volume(&map->boxes[i]);
        if (volume == 0)
        {
            return false;
        }
        sum += volume;
    }
    return sum == map->volume;
}

static void Push(Map* map, mrhiBox box)
{
    // The boxes are sized from the accesses.
    if (map->nextCount == map->half)
    {
        map->full = mrhi_diagnosticFrameAccesses;
        return;
    }
    map->next[map->nextCount++] = box;
}

// Whether the barriers that are the program's have reached its limit.
static bool IsProgramBudgetFull(const mrhiDevice* device)
{
    return device->frameBarrierCount - device->frameLibraryBarriers ==
           device->budgets.frameBarriers;
}

static void Emit(Map* map, uint32_t pass, const mrhiBox* part, mrhiResourceState after)
{
    mrhiDevice* device = map->device;
    // A barrier at the frame's end only joins other states; any other
    // comes before a kept pass.
    MRHI_ASSERT(pass != 0 || part->state != after);
    MRHI_ASSERT(pass == 0 || device->framePasses[pass - 1].kept);
    MRHI_ASSERT(part->mipCount > 0 && part->layerCount > 0 && part->planes != 0);
    if (map->library ? device->frameBarrierCount == device->frameBarrierLimit
                     : IsProgramBudgetFull(device))
    {
        map->full = mrhi_diagnosticFrameBarriers;
        return;
    }
    device->frameLibraryBarriers += map->library ? 1 : 0;
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

// Orders a pass after the passes of other classes a part's tracking
// names.
static void Follow(mrhiFramePass* pass, const uint32_t* passes)
{
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        if (c != pass->passClass && passes[c] > pass->waitFor[c])
        {
            pass->waitFor[c] = passes[c];
        }
    }
}

// Joins a part's tracking into the box a use leaves: by class, the later
// of each, which over-orders a pass at worst, never under-orders it.
static void Join(mrhiBox* into, const mrhiBox* part)
{
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        into->users[c] = part->users[c] > into->users[c] ? part->users[c] : into->users[c];
        into->setters[c] =
            part->setters[c] > into->setters[c] ? part->setters[c] : into->setters[c];
    }
}

// Moves a part of the map to a state, before a pass or at the frame's end
// (pass 0): barriers from every state it meets, at the end only from
// other states. Before a pass, the pass follows the other classes' users
// of each part it races, and the passes that set each other part's state;
// a part it changes or writes is then set by it alone.
static void Move(Map* map, uint32_t pass, mrhiBox cut)
{
    mrhiFramePass* framePass = pass == 0 ? nullptr : &map->device->framePasses[pass - 1];
    bool sets = false;
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
        if (framePass != nullptr)
        {
            Follow(framePass, needed ? box->users : box->setters);
            bool set = needed || mrhiStateWrites(cut.state);
            sets = sets || set;
            if (!set)
            {
                Join(&cut, box);
            }
        }
        PushRest(map, box, &cut, &part);
    }
    if (framePass != nullptr)
    {
        cut.users[framePass->passClass] = pass;
        cut.setters[framePass->passClass] = sets ? pass : cut.setters[framePass->passClass];
    }
    Push(map, cut);
    Swap(map);
    MRHI_ASSERT(map->full != 0 || CoversWhole(map));
}

// Forgets the kept passes a resource's last plan found.
static void ForgetPasses(mrhiFrameResource* resource)
{
    resource->firstPass = 0;
    resource->lastPass = 0;
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        resource->lastPassOf[c] = 0;
    }
}

// Whether a use in pass index p makes the library's barriers: one of the
// first pass's, which a texture's clear takes from the undefined state,
// or the first program use of a buffer the clear left in its state,
// after which it no longer is.
static bool IsLibraryMove(bool* zeroState, uint32_t p)
{
    bool library = *zeroState && p >= MRHI_LIBRARY_PASSES;
    *zeroState = *zeroState && !library;
    return library || p < MRHI_LIBRARY_PASSES;
}

// Plans one resource over the kept passes, then its end: an imported
// object's parts all move to its last use's state. Returns the code of
// the limit it found full, 0 for none.
static mrhiDiagnosticCode PlanResource(mrhiDevice* device, uint32_t slot)
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
    map.volume = Volume(&whole);
    ForgetPasses(resource);
    resource->finalState = whole.state;
    bool targetsOnly = true;
    // The first program use of a buffer the clear left in its state.
    bool zeroState = resource->zeroState;
    for (uint32_t p = 0; p < device->framePassCount && map.full == 0; ++p)
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
            resource->lastPassOf[pass->passClass] = p + 1;
            resource->finalState = uses[i].state;
            bool target = uses[i].use == mrhiUseColorTarget || uses[i].use == mrhiUseDepthTarget;
            targetsOnly = targetsOnly && target && uses[i].writes && !uses[i].reads;
            mrhiBox cut = BoxOf(resource, &uses[i]);
            // The pass's checks keep every use inside its resource.
            MRHI_ASSERT(cut.baseMip + cut.mipCount <= whole.mipCount &&
                        cut.baseLayer + cut.layerCount <= whole.layerCount &&
                        (cut.planes & ~whole.planes) == 0);
            map.library = IsLibraryMove(&zeroState, p);
            Move(&map, p + 1, cut);
            map.library = false;
        }
    }
    // A texture with view formats may be rendered in one, which WebGPU's
    // transient attachments refuse.
    resource->transient = resource->kind == mrhiFrameTexture && resource->firstPass != 0 &&
                          targetsOnly && resource->texture.viewFormats[0] == mrhi_formatNone;
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
    if (resource->resting != mrhi_stateUndefined)
    {
        resource->finalState = resource->resting;
    }
    if (mrhiOutlivesFrame(resource))
    {
        whole.state = resource->finalState;
        Move(&map, 0, whole);
    }
    return map.full;
}

// Whether the barriers are in the order they run: by pass, the frame's
// end last. Only an assertion calls it.
[[maybe_unused]] static bool InRunOrder(const mrhiDevice* device)
{
    uint32_t last = 1;
    for (uint32_t i = 0; i < device->frameBarrierCount; ++i)
    {
        uint32_t pass = device->frameBarriers[i].pass.index1;
        uint32_t rank = pass == 0 ? UINT32_MAX : pass;
        if (rank < last)
        {
            return false;
        }
        last = rank;
    }
    return true;
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
        MRHI_ASSERT(pass <= device->framePassCount);
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
    MRHI_ASSERT(InRunOrder(device));
}

// Whether a barrier before a pass after one, up to another, meets a part
// of a resource. Only assertions call it.
[[maybe_unused]] static bool BarrierMeets(const mrhiDevice* device, uint32_t after, uint32_t upTo,
                                          uint32_t slot, const mrhiBox* part)
{
    uint8_t planes = WholeOf(&device->frameResources[slot - 1]).planes;
    for (uint32_t i = 0; i < device->frameBarrierCount; ++i)
    {
        const mrhiBarrier* barrier = &device->frameBarriers[i];
        const mrhiTextureRange* range = &barrier->range;
        uint8_t covered = range->aspect == mrhi_aspectAll         ? planes
                          : range->aspect == mrhi_aspectDepthOnly ? 1
                                                                  : 2;
        mrhiBox box = {
            .baseMip = range->baseMip,
            .mipCount = range->mipCount,
            .baseLayer = range->baseLayer,
            .layerCount = range->layerCount,
            .planes = covered,
            .state = barrier->after,
        };
        mrhiBox common;
        uint32_t pass = barrier->pass.index1;
        if (pass > after && pass <= upTo && barrier->resource.index1 == slot &&
            Intersect(&box, part, &common))
        {
            return true;
        }
    }
    return false;
}

// Whether two uses of one part race without a barrier between them:
// either writes, or a texture changes state. Only assertions call it.
[[maybe_unused]] static bool Races(const mrhiFrameResource* resource, const mrhiFrameUse* first,
                                   const mrhiFrameUse* second)
{
    return mrhiStateWrites(first->state) || mrhiStateWrites(second->state) ||
           (IsTexture(resource) && first->state != second->state);
}

// Whether no hazard was planned: for every two kept uses of a part of a
// resource in different passes that race, a barrier before a pass after
// the first, up to the second, meets the part they share. It checks the
// barriers against the uses alone, apart from how the maps found them,
// at the cost of every pair of uses. Only an assertion calls it.
[[maybe_unused]] static bool IsHazardFree(const mrhiDevice* device)
{
    for (uint32_t q = 1; q < device->framePassCount; ++q)
    {
        const mrhiFramePass* later = &device->framePasses[q];
        for (uint32_t j = 0; j < later->useCount && later->kept; ++j)
        {
            const mrhiFrameUse* second = &device->frameUses[later->firstUse + j];
            const mrhiFrameResource* resource = &device->frameResources[second->resource - 1];
            mrhiBox box = BoxOf(resource, second);
            for (uint32_t p = 0; p < q; ++p)
            {
                const mrhiFramePass* earlier = &device->framePasses[p];
                for (uint32_t i = 0; i < earlier->useCount && earlier->kept; ++i)
                {
                    const mrhiFrameUse* first = &device->frameUses[earlier->firstUse + i];
                    if (first->resource != second->resource)
                    {
                        continue;
                    }
                    mrhiBox earlierBox = BoxOf(resource, first);
                    mrhiBox shared;
                    if (Intersect(&earlierBox, &box, &shared) && Races(resource, first, second) &&
                        !BarrierMeets(device, p + 1, q + 1, second->resource, &shared))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

mrhiResult mrhiPlan(mrhiDevice* device)
{
    device->frameBarrierCount = 0;
    device->frameLibraryBarriers = 0;
    for (uint32_t p = 0; p < device->framePassCount; ++p)
    {
        for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
        {
            device->framePasses[p].waitFor[c] = 0;
        }
    }
    for (uint32_t slot = 1; slot <= device->frameResourceCount; ++slot)
    {
        mrhiDiagnosticCode full = PlanResource(device, slot);
        if (full != 0)
        {
            return mrhiDeviceFull(device, full);
        }
    }
    Sort(device);
    MRHI_ASSERT(IsHazardFree(device));
    return mrhi_success;
}

// Whether another placed resource used memory a placed resource takes, in
// kept passes before its first.
static bool UsedBefore(const mrhiFrameResource* other, const mrhiFrameResource* resource)
{
    return other->placed && other->lastPass < resource->firstPass &&
           other->memoryOffset < resource->memoryOffset + resource->memoryBytes &&
           resource->memoryOffset < other->memoryOffset + other->memoryBytes;
}

// Whether a placed resource takes memory that another placed resource
// used in kept passes before its first.
static bool Aliases(const mrhiDevice* device, const mrhiFrameResource* resource)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        if (UsedBefore(&device->frameResources[i], resource))
        {
            return true;
        }
    }
    return false;
}

// Orders every kept pass using a placed resource after the other classes'
// last passes that used the memory it takes before it: their uses and its
// meet in the same bytes, even where it reads what it never wrote.
static void FollowAliased(mrhiDevice* device, uint32_t slot)
{
    const mrhiFrameResource* resource = &device->frameResources[slot - 1];
    uint32_t after[MRHI_PASS_CLASSES] = {0};
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* other = &device->frameResources[i];
        for (uint32_t c = 0; c < MRHI_PASS_CLASSES && UsedBefore(other, resource); ++c)
        {
            after[c] = other->lastPassOf[c] > after[c] ? other->lastPassOf[c] : after[c];
        }
    }
    for (uint32_t p = resource->firstPass - 1; p < resource->lastPass; ++p)
    {
        mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        for (uint32_t i = 0; i < pass->useCount && pass->kept; ++i)
        {
            if (uses[i].resource == slot)
            {
                Follow(pass, after);
                break;
            }
        }
    }
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
        // Only a resource a kept pass uses is placed.
        MRHI_ASSERT(resource->firstPass != 0);
        FollowAliased(device, slot);
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
        if (IsProgramBudgetFull(device))
        {
            return mrhiDeviceFull(device, mrhi_diagnosticFrameBarriers);
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

// Whether a kept pass follows an earlier kept pass, through waits and
// class order. Only assertions call it.
[[maybe_unused]] static bool Follows(const mrhiDevice* device, uint32_t later, uint32_t earlier)
{
    const mrhiFramePass* first = &device->framePasses[earlier - 1];
    return device->framePasses[later - 1].follows[first->passClass] >= earlier;
}

// Whether a kept pass uses a resource. Only assertions call it.
[[maybe_unused]] static bool Uses(const mrhiDevice* device, uint32_t pass, uint32_t slot)
{
    const mrhiFramePass* framePass = &device->framePasses[pass - 1];
    for (uint32_t i = 0; i < framePass->useCount && framePass->kept; ++i)
    {
        if (device->frameUses[framePass->firstUse + i].resource == slot)
        {
            return true;
        }
    }
    return false;
}

// Whether every two kept uses of a part of a resource follow one another,
// whatever their classes, when they race or a barrier before the earlier
// sets the part's state for the later: apart from how the maps found the
// waits, at the cost of every pair of uses. Only assertions call it.
[[maybe_unused]] static bool RacesFollow(const mrhiDevice* device)
{
    for (uint32_t q = 1; q < device->framePassCount; ++q)
    {
        const mrhiFramePass* later = &device->framePasses[q];
        for (uint32_t j = 0; j < later->useCount && later->kept; ++j)
        {
            const mrhiFrameUse* second = &device->frameUses[later->firstUse + j];
            const mrhiFrameResource* resource = &device->frameResources[second->resource - 1];
            mrhiBox box = BoxOf(resource, second);
            for (uint32_t p = 0; p < q; ++p)
            {
                const mrhiFramePass* earlier = &device->framePasses[p];
                for (uint32_t i = 0; i < earlier->useCount && earlier->kept; ++i)
                {
                    const mrhiFrameUse* first = &device->frameUses[earlier->firstUse + i];
                    mrhiBox earlierBox = BoxOf(resource, first);
                    mrhiBox shared;
                    bool ordered = first->resource != second->resource ||
                                   !Intersect(&earlierBox, &box, &shared) ||
                                   Follows(device, q + 1, p + 1);
                    if (!ordered && (Races(resource, first, second) ||
                                     BarrierMeets(device, p, p + 1, second->resource, &shared)))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Whether every kept use of a placed resource follows every kept use of
// the resources that used its memory before it. Only assertions call it.
[[maybe_unused]] static bool MemoryFollows(const mrhiDevice* device)
{
    for (uint32_t r = 1; r <= device->frameResourceCount; ++r)
    {
        const mrhiFrameResource* resource = &device->frameResources[r - 1];
        for (uint32_t o = 1; o <= device->frameResourceCount && resource->placed; ++o)
        {
            const mrhiFrameResource* other = &device->frameResources[o - 1];
            for (uint32_t p = other->firstPass; UsedBefore(other, resource) && p <= other->lastPass;
                 ++p)
            {
                for (uint32_t q = resource->firstPass;
                     q <= resource->lastPass && Uses(device, p, o); ++q)
                {
                    if (Uses(device, q, r) && !Follows(device, q, p))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Whether every kept pass follows the native passes before it and is
// followed by those after it. Only assertions call it.
[[maybe_unused]] static bool NativesFollow(const mrhiDevice* device)
{
    for (uint32_t n = 1; n <= device->framePassCount; ++n)
    {
        const mrhiFramePass* native = &device->framePasses[n - 1];
        for (uint32_t x = 1; x <= device->framePassCount && native->kept && native->native; ++x)
        {
            bool ordered = x == n || !device->framePasses[x - 1].kept ||
                           (x < n ? Follows(device, n, x) : Follows(device, x, n));
            if (!ordered)
            {
                return false;
            }
        }
    }
    return true;
}

// Whether a pass declares a query resolve access, so that it may resolve
// queries.
static bool MayResolve(const mrhiDevice* device, const mrhiFramePass* pass)
{
    for (uint32_t i = 0; i < pass->useCount; ++i)
    {
        if (device->frameUses[pass->firstUse + i].state == mrhi_stateQueryResolve)
        {
            return true;
        }
    }
    return false;
}

// Whether every kept pass that may resolve queries follows every kept
// timed pass before it, and every kept timed pass follows every kept pass
// before it that may resolve. Only assertions call it.
[[maybe_unused]] static bool TimestampsFollow(const mrhiDevice* device)
{
    for (uint32_t q = 2; q <= device->framePassCount; ++q)
    {
        const mrhiFramePass* later = &device->framePasses[q - 1];
        for (uint32_t p = 1; p < q && later->kept; ++p)
        {
            const mrhiFramePass* earlier = &device->framePasses[p - 1];
            bool meet =
                earlier->kept && ((earlier->timestampSet != 0 && MayResolve(device, later)) ||
                                  (MayResolve(device, earlier) && later->timestampSet != 0));
            if (meet && !Follows(device, q, p))
            {
                return false;
            }
        }
    }
    return true;
}

// Finds the passes a kept pass follows: those its class's previous pass
// follows, those each pass it waits for follows, and itself.
static void FindFollows(mrhiDevice* device, uint32_t index, uint32_t previous)
{
    mrhiFramePass* pass = &device->framePasses[index - 1];
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        pass->follows[c] = previous == 0 ? 0 : device->framePasses[previous - 1].follows[c];
    }
    for (uint32_t w = 0; w < MRHI_PASS_CLASSES; ++w)
    {
        const uint32_t* waited =
            pass->waitFor[w] == 0 ? nullptr : device->framePasses[pass->waitFor[w] - 1].follows;
        for (uint32_t c = 0; c < MRHI_PASS_CLASSES && waited != nullptr; ++c)
        {
            pass->follows[c] = waited[c] > pass->follows[c] ? waited[c] : pass->follows[c];
        }
    }
    pass->follows[pass->passClass] = index;
}

void mrhiOrderPasses(mrhiDevice* device)
{
    uint32_t latest[MRHI_PASS_CLASSES] = {0};
    uint32_t natives[MRHI_PASS_CLASSES] = {0};
    uint32_t timed[MRHI_PASS_CLASSES] = {0};
    uint32_t resolving[MRHI_PASS_CLASSES] = {0};
    for (uint32_t p = 0; p < device->framePassCount; ++p)
    {
        mrhiFramePass* pass = &device->framePasses[p];
        if (!pass->kept)
        {
            continue;
        }
        // A resolve is recorded after the compile, so a pass that may
        // resolve follows every class's timed passes before it, and a
        // timed pass follows the passes before it that may resolve: a
        // query reads as written when an earlier pass in the frame's order
        // wrote it, on every queue.
        bool resolves = MayResolve(device, pass);
        if (resolves)
        {
            Follow(pass, timed);
        }
        if (pass->timestampSet != 0)
        {
            Follow(pass, resolving);
        }
        // A native pass follows every class's passes before it, and every
        // class's passes after it follow it: the program's commands may
        // reach past what it declares.
        Follow(pass, natives);
        if (pass->native)
        {
            Follow(pass, latest);
            natives[pass->passClass] = p + 1;
        }
        FindFollows(device, p + 1, latest[pass->passClass]);
        latest[pass->passClass] = p + 1;
        timed[pass->passClass] = pass->timestampSet != 0 ? p + 1 : timed[pass->passClass];
        resolving[pass->passClass] = resolves ? p + 1 : resolving[pass->passClass];
    }
    MRHI_ASSERT(RacesFollow(device));
    MRHI_ASSERT(MemoryFollows(device));
    MRHI_ASSERT(NativesFollow(device));
    MRHI_ASSERT(TimestampsFollow(device));
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
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    size_t count = device->frameBarrierCount;
    for (size_t i = 0; i < count && i < capacity; ++i)
    {
        mrhiOutBarrier(&barriers[i], &device->frameBarriers[i]);
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
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
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
    const mrhiResourcePlan plan = {
        .usage = planned->usage,
        .transient = planned->transient,
        .firstPass = {planned->firstPass, planned->firstPass == 0 ? 0 : serial},
        .lastPass = {planned->lastPass, planned->lastPass == 0 ? 0 : serial},
        .memoryOffset = planned->memoryOffset,
        .memoryBytes = planned->memoryBytes,
    };
    mrhiOutResourcePlan(planOut, &plan);
    return mrhi_success;
}
