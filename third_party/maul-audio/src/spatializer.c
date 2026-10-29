// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spatializers. Sources live in slots named by index and generation; a
// destroyed slot's generation moves on, so its old ids are stale at
// once, and the slot may be reused at once: every published entry
// carries the generation it was computed for, and the rendering side
// holds nothing of a source beyond that copy. Steps are published
// through three buffers (one being written, one latched by the
// rendering side, one waiting): `shared` holds the waiting buffer's
// index and whether it is newer than the latched one, and each side
// swaps its own buffer with it atomically.

#include "maul-audio/spatializer.h"

#include "air_absorption.h"
#include "allocator.h"
#include "direct_step.h"
#include "fpenv.h"
#include "host_tasks.h"
#include "occlusion.h"
#include "pathing.h"
#include "probe_sets.h"
#include "reflections.h"
#include "reverb_estimate.h"
#include "spatializer_state.h"
#include "transmission.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define SOURCE_DEF_COOKIE 0x6D61736Fu
// Rays per round of queries, and per query.
#define ROUND 4096u
#define BATCH 64u
// A reverberation ray's bounces at most; past them the decay is filled
// in at its rate.
#define REVERB_BOUNCES 1024u
#define NO_REVERB      0.1f
#define SILENT         (-96.0f)

maudSourceDef maudDefaultSourceDef(void)
{
    return (maudSourceDef){
        .cookie = SOURCE_DEF_COOKIE,
        .directivity = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        .occlusion = maud_occlusionRay,
        .occlusionRadius = 1.0f,
        .occlusionSamples = 32,
        .enableTransmission = true,
        .enablePathing = true,
    };
}

static size_t SlotBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(Slot);
}

static size_t FreeBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(uint32_t);
}

static size_t EntryBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(Entry);
}

static void Free(maudAllocator* allocator, void* memory, size_t bytes, size_t alignment)
{
    if (memory != nullptr)
    {
        maudRelease(allocator, memory, bytes, alignment);
    }
}

static void Release(maudSpatializer* s)
{
    maudAllocator allocator = s->allocator;
    Free(&allocator, s->points, (size_t)s->maxSamples * sizeof(maudVector3), alignof(maudVector3));
    Free(&allocator, s->rays, ROUND * sizeof(maudRay), alignof(maudRay));
    Free(&allocator, s->occluded, ROUND, 1);
    Free(&allocator, s->offsets, FreeBytes(s->capacity), alignof(uint32_t));
    Free(&allocator, s->materials, (size_t)s->materialCapacity * sizeof(maudAcousticMaterial),
         alignof(maudAcousticMaterial));
    Free(&allocator, s->hits, ROUND * sizeof(maudRayHit), alignof(maudRayHit));
    Free(&allocator, s->walkers, FreeBytes(s->capacity), alignof(uint32_t));
    Free(&allocator, s->walked, (size_t)s->capacity * sizeof(float), alignof(float));
    maudDestroyReflections(s->reflections);
    maudDestroyProbeSets(s->probeSets);
    maudDestroyPathing(s->pathing);
    Free(&allocator, s->histograms,
         (size_t)maudReverbBatches(s->trace.rays) * sizeof(maudReverbHistogram),
         alignof(maudReverbHistogram));
    for (int b = 0; b < 3; ++b)
    {
        if (s->buffers[b].entries != nullptr)
        {
            maudRelease(&allocator, s->buffers[b].entries, EntryBytes(s->capacity), alignof(Entry));
        }
    }
    if (s->free != nullptr)
    {
        maudRelease(&allocator, s->free, FreeBytes(s->capacity), alignof(uint32_t));
    }
    if (s->slots != nullptr)
    {
        maudRelease(&allocator, s->slots, SlotBytes(s->capacity), alignof(Slot));
    }
    maudRelease(&allocator, s, sizeof(maudSpatializer), alignof(maudSpatializer));
}

static bool ReverbValid(const maudSpatializerDef* def)
{
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        if (!(def->airAbsorption[b] >= 0.0f) || !isfinite(def->airAbsorption[b]))
        {
            return false;
        }
    }
    bool reflections = def->reflectionOrder == 0 ||
                       (def->reverbRays > 0 && def->reflectionDuration >= 0.05f &&
                        def->reflectionDuration <= 4.0f && def->reflectionRate >= 44100 &&
                        def->reflectionRate <= 384000);
    return reflections;
}

// Whether a count passes one of the published bounds.
static bool PastLimits(const maudSpatializerDef* def)
{
    return def->sourceCapacity > MAUD_MAX_SOURCES ||
           def->maxOcclusionSamples > MAUD_MAX_OCCLUSION_SAMPLES ||
           def->maxSurfaces > MAUD_MAX_SURFACES || def->materialCapacity > MAUD_MAX_MATERIALS ||
           (def->reverbRays != 0 &&
            (def->reverbRays < MAUD_MIN_REVERB_RAYS || def->reverbRays > MAUD_MAX_REVERB_RAYS)) ||
           def->reflectionOrder > MAUD_MAX_REFLECTION_ORDER ||
           def->probeSetCapacity > MAUD_MAX_PROBE_SETS || def->maxProbes > MAUD_MAX_PROBES ||
           def->maxProbePairs > MAUD_MAX_PROBE_PAIRS || def->maxPaths > MAUD_MAX_PATHS;
}

// Allocates everything a spatializer holds but itself; false if any
// allocation failed (Release frees what was made).
static bool Allocate(maudSpatializer* s, const maudSpatializerDef* def)
{
    s->points = maudAllocate(&def->allocator, (size_t)s->maxSamples * sizeof(maudVector3),
                             alignof(maudVector3));
    s->rays = maudAllocate(&def->allocator, ROUND * sizeof(maudRay), alignof(maudRay));
    s->occluded = maudAllocate(&def->allocator, ROUND, 1);
    s->offsets = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    s->materials =
        maudAllocate(&def->allocator, (size_t)s->materialCapacity * sizeof(maudAcousticMaterial),
                     alignof(maudAcousticMaterial));
    s->hits = maudAllocate(&def->allocator, ROUND * sizeof(maudRayHit), alignof(maudRayHit));
    s->walkers = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    s->walked = maudAllocate(&def->allocator, (size_t)s->capacity * sizeof(float), alignof(float));
    if (def->reverbRays > 0)
    {
        s->histograms =
            maudAllocate(&def->allocator,
                         (size_t)maudReverbBatches(def->reverbRays) * sizeof(maudReverbHistogram),
                         alignof(maudReverbHistogram));
    }
    s->slots = maudAllocate(&def->allocator, SlotBytes(s->capacity), alignof(Slot));
    s->free = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    bool all = s->slots != nullptr && s->free != nullptr && s->points != nullptr &&
               s->rays != nullptr && s->occluded != nullptr && s->offsets != nullptr &&
               s->materials != nullptr && s->hits != nullptr && s->walkers != nullptr &&
               s->walked != nullptr && (def->reverbRays == 0 || s->histograms != nullptr);
    if (all && def->reflectionOrder > 0)
    {
        uint32_t batches = maudReverbBatches(def->reverbRays);
        s->reflections =
            maudCreateReflections(&def->allocator, def->reflectionOrder, def->reflectionDuration,
                                  (float)def->reflectionRate, batches);
        all = s->reflections != nullptr;
        if (all)
        {
            maudPrepareReflections(s->reflections, &s->trace, s->histograms, batches);
        }
    }
    if (all && def->probeSetCapacity > 0)
    {
        s->probeSets = maudCreateProbeSets(&def->allocator, def->probeSetCapacity);
        all = s->probeSets != nullptr;
    }
    if (all && def->probeSetCapacity > 0 && def->maxPaths > 0)
    {
        s->pathing = maudCreatePathing(&def->allocator, def->maxProbes, def->maxPaths);
        all = s->pathing != nullptr;
    }
    for (int b = 0; b < 3; ++b)
    {
        s->buffers[b].entries =
            maudAllocate(&def->allocator, EntryBytes(s->capacity), alignof(Entry));
        all = all && s->buffers[b].entries != nullptr;
    }
    return all;
}

static MAUD_NOINLINE maudResult CreateSpatializer(const maudSpatializerDef* def,
                                                  maudSpatializer** spatializerOut)
{
    if (spatializerOut != nullptr)
    {
        *spatializerOut = nullptr;
    }
    if (def != nullptr && def->cookie == MAUD_SPATIALIZER_DEF_COOKIE &&
        def->version != MAUD_ABI_VERSION)
    {
        return maud_errorVersion;
    }
    if (def != nullptr && spatializerOut != nullptr && def->cookie == MAUD_SPATIALIZER_DEF_COOKIE &&
        PastLimits(def))
    {
        return maud_errorLimit;
    }
    if (def == nullptr || spatializerOut == nullptr || def->cookie != MAUD_SPATIALIZER_DEF_COOKIE ||
        def->sourceCapacity == 0 || def->maxOcclusionSamples == 0 || def->maxSurfaces == 0 ||
        def->materialCapacity == 0 ||
        (def->enqueueTask == nullptr) != (def->finishTask == nullptr) || !ReverbValid(def) ||
        def->maxProbes == 0 || def->maxProbePairs == 0 || !maudIsAllocatorValid(&def->allocator))
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s =
        maudAllocate(&def->allocator, sizeof(maudSpatializer), alignof(maudSpatializer));
    if (s == nullptr)
    {
        return maud_errorCapacity;
    }
    *s = (maudSpatializer){
        .allocator = def->allocator,
        .capacity = def->sourceCapacity,
        .maxSamples = def->maxOcclusionSamples,
        .anyHit = def->anyHit,
        .closestHit = def->closestHit,
        .rayContext = def->rayContext,
        .maxSurfaces = def->maxSurfaces,
        .materialCapacity = def->materialCapacity,
        .enqueueTask = def->enqueueTask,
        .finishTask = def->finishTask,
        .userTaskContext = def->userTaskContext,
        .trace = {.air = {def->airAbsorption[0], def->airAbsorption[1], def->airAbsorption[2]},
                  .rays = def->reverbRays,
                  .maxBounces = REVERB_BOUNCES},
        .reverb = {.reverbTime = {NO_REVERB, NO_REVERB, NO_REVERB},
                   .tailLevel = {SILENT, SILENT, SILENT}},
        .reflectionDuration = def->reflectionDuration,
        .maxProbes = def->maxProbes,
        .maxProbePairs = def->maxProbePairs,
        .maxPaths = def->maxPaths,
    };
    bool all = Allocate(s, def);
    if (!all)
    {
        Release(s);
        return maud_errorCapacity;
    }
    for (uint32_t i = 0; i < s->capacity; ++i)
    {
        s->slots[i] = (Slot){.generation = 1};
        // Taken from the end: the lowest index first.
        s->free[i] = s->capacity - 1 - i;
    }
    s->freeCount = s->capacity;
    maudBallPoints(s->maxSamples, s->points);
    for (int b = 0; b < 3; ++b)
    {
        memset(s->buffers[b].entries, 0, EntryBytes(s->capacity));
        s->buffers[b].reverb = s->reverb;
    }
    s->back = 0;
    s->front = 1;
    atomic_init(&s->shared, 2u);
    atomic_init(&s->misuse, 0);
    atomic_init(&s->host, false);
    *spatializerOut = s;
    return maud_success;
}

// The occlusion's sample points and the reflections' tables are made
// here, so creation is a computing entry too.
maudResult maudCreateSpatializer(const maudSpatializerDef* def, maudSpatializer** spatializerOut)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = CreateSpatializer(def, spatializerOut);
    maudFloatEnvLeave(token);
    return result;
}

void maudDestroySpatializer(maudSpatializer* spatializer)
{
    if (spatializer != nullptr && !maudSpatializerBusy(spatializer))
    {
        // Its allocator runs from here on, with no handle left valid.
        maudSpatializerRunHost(spatializer, true);
        Release(spatializer);
    }
}

uint64_t maudSpatializer_GetMisuse(const maudSpatializer* spatializer)
{
    return atomic_load_explicit(&spatializer->misuse, memory_order_relaxed);
}

maudResult maudSpatializerInvalid(maudSpatializer* s)
{
    atomic_fetch_add_explicit(&s->misuse, 1, memory_order_relaxed);
    return maud_errorInvalid;
}

bool maudSpatializerBusy(maudSpatializer* s)
{
    if (atomic_load_explicit(&s->host, memory_order_acquire))
    {
        atomic_fetch_add_explicit(&s->misuse, 1, memory_order_relaxed);
        return true;
    }
    return false;
}

void maudSpatializerRunHost(maudSpatializer* s, bool running)
{
    atomic_store_explicit(&s->host, running, memory_order_release);
}

static bool PatternValid(const maudDirectivityPattern* pattern)
{
    float out[MAUD_BANDS];
    return maudGetDirectivity(pattern, (maudVector3){0.0f, 0.0f, -1.0f}, out) == maud_success;
}

static MAUD_NOINLINE maudResult CreateSource(maudSpatializer* spatializer, const maudSourceDef* def,
                                             maudSourceId* sourceOut)
{
    if (sourceOut != nullptr)
    {
        *sourceOut = (maudSourceId){0, 0};
    }
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudSpatializerBusy(spatializer))
    {
        return maud_errorBusy;
    }
    if (def == nullptr || sourceOut == nullptr || def->cookie != SOURCE_DEF_COOKIE ||
        !PatternValid(&def->directivity) || def->occlusion > maud_occlusionVolumetric ||
        (def->occlusion == maud_occlusionVolumetric &&
         (!(def->occlusionRadius > 0.0f) || !isfinite(def->occlusionRadius) ||
          def->occlusionSamples == 0 || def->occlusionSamples > spatializer->maxSamples)))
    {
        return maudSpatializerInvalid(spatializer);
    }
    if (spatializer->freeCount == 0)
    {
        return maud_errorCapacity;
    }
    uint32_t index = spatializer->free[--spatializer->freeCount];
    Slot* slot = &spatializer->slots[index];
    slot->live = true;
    slot->pose = (maudPose){{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    slot->directivity = def->directivity;
    slot->occlusion = def->occlusion;
    slot->radius = def->occlusionRadius;
    slot->samples = def->occlusionSamples;
    slot->transmission = def->enableTransmission;
    slot->pathing = def->enablePathing;
    *sourceOut = (maudSourceId){index + 1, slot->generation};
    return maud_success;
}

// Checking a def compares floats, which denormals-are-zero would read
// otherwise: the host's mode must not decide what is accepted.
maudResult maudCreateSource(maudSpatializer* spatializer, const maudSourceDef* def,
                            maudSourceId* sourceOut)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = CreateSource(spatializer, def, sourceOut);
    maudFloatEnvLeave(token);
    return result;
}

// The live slot an id names for a call that changes it, or the reason
// there is none.
static maudResult Find(maudSpatializer* spatializer, maudSourceId source, Slot** slotOut)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudSpatializerBusy(spatializer))
    {
        return maud_errorBusy;
    }
    if (source.index1 == 0 || source.index1 > spatializer->capacity || source.generation == 0)
    {
        return maudSpatializerInvalid(spatializer);
    }
    Slot* slot = &spatializer->slots[source.index1 - 1];
    if (!slot->live || slot->generation != source.generation)
    {
        return maud_errorStale;
    }
    *slotOut = slot;
    return maud_success;
}

maudResult maudDestroySource(maudSpatializer* spatializer, maudSourceId source)
{
    Slot* slot = nullptr;
    maudResult result = Find(spatializer, source, &slot);
    if (result != maud_success)
    {
        return result;
    }
    slot->live = false;
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    spatializer->free[spatializer->freeCount++] = source.index1 - 1;
    return maud_success;
}

static MAUD_NOINLINE maudResult SetSourcePose(maudSpatializer* spatializer, maudSourceId source,
                                              const maudPose* pose)
{
    Slot* slot = nullptr;
    maudResult result = Find(spatializer, source, &slot);
    if (result != maud_success)
    {
        return result;
    }
    if (pose == nullptr || !maudPoseValid(pose))
    {
        return maudSpatializerInvalid(spatializer);
    }
    slot->pose = *pose;
    return maud_success;
}

maudResult maudSource_SetPose(maudSpatializer* spatializer, maudSourceId source,
                              const maudPose* pose)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = SetSourcePose(spatializer, source, pose);
    maudFloatEnvLeave(token);
    return result;
}

// Queries batches [start, end) of the round's rays.
static MAUD_NOINLINE void QueryBatches(uint32_t start, uint32_t end, void* context)
{
    maudSpatializer* s = context;
    for (uint32_t b = start; b < end; ++b)
    {
        uint32_t first = b * BATCH;
        uint32_t count = s->roundRays - first < BATCH ? s->roundRays - first : BATCH;
        if (s->closest)
        {
            s->closestHit(s->rays + first, count, s->hits + first, s->rayContext);
        }
        else
        {
            s->anyHit(s->rays + first, count, s->occluded + first, s->rayContext);
        }
    }
}

static void Query(maudSpatializer* s)
{
    uint32_t batches = (s->roundRays + BATCH - 1) / BATCH;
    maudHostTasks hooks = {s->enqueueTask, s->finishTask, s->userTaskContext};
    maudRunHostTasks(&hooks, QueryBatches, batches, s);
}

static uint32_t RaysOf(const maudSpatializer* s, const Slot* slot)
{
    return slot->live && s->anyHit != nullptr
               ? maudOcclusionRayCount(slot->occlusion, slot->samples)
               : 0;
}

// The occlusion of every source: rounds of whole sources' rays, in slot
// order, each queried and then reduced into the buffer's entries.
static void Occlude(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    uint32_t next = 0;
    while (next < s->capacity)
    {
        uint32_t first = next;
        s->roundRays = 0;
        s->closest = false;
        for (; next < s->capacity; ++next)
        {
            const Slot* slot = &s->slots[next];
            uint32_t count = RaysOf(s, slot);
            if (s->roundRays + count > ROUND)
            {
                break;
            }
            s->offsets[next] = s->roundRays;
            if (count > 0)
            {
                maudOcclusionRays(listener, &slot->pose, slot->occlusion, slot->radius,
                                  slot->samples, s->points, s->rays + s->roundRays);
            }
            s->roundRays += count;
        }
        Query(s);
        for (uint32_t i = first; i < next; ++i)
        {
            const Slot* slot = &s->slots[i];
            if (RaysOf(s, slot) > 0)
            {
                entries[i].result.occlusion =
                    maudOcclusionOf(slot->occlusion, slot->samples, s->occluded + s->offsets[i]);
            }
        }
    }
}

// One surface of every walking source's path, in rounds of ROUND
// sources; the walkers still going stay listed in order.
static void Cross(maudSpatializer* s, const maudPose* listener, Entry* entries, uint32_t* walking)
{
    uint32_t kept = 0;
    for (uint32_t first = 0; first < *walking; first += ROUND)
    {
        uint32_t count = *walking - first < ROUND ? *walking - first : ROUND;
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t i = s->walkers[first + k];
            s->rays[k] = maudPathRay(listener, &s->slots[i].pose, s->walked[i]);
        }
        s->roundRays = count;
        s->closest = true;
        Query(s);
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t i = s->walkers[first + k];
            maudDirectResult* r = &entries[i].result;
            if (maudCrossSurface(&s->hits[k], &s->rays[k], s->materials, s->materialCount,
                                 r->transmission, &s->walked[i]))
            {
                r->surfaceCount += 1;
                s->walkers[kept++] = i;
            }
        }
    }
    *walking = kept;
}

// The transmission of every occluded source: walked surface by surface
// where the source asks and the host answers closest hits, otherwise 0.
static void Transmit(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    uint32_t walking = 0;
    for (uint32_t i = 0; i < s->capacity; ++i)
    {
        const Slot* slot = &s->slots[i];
        maudDirectResult* r = &entries[i].result;
        if (!slot->live || r->occlusion == 0.0f)
        {
            continue;
        }
        if (slot->transmission && s->closestHit != nullptr)
        {
            s->walkers[walking++] = i;
            s->walked[i] = 0.0f;
            continue;
        }
        for (int b = 0; b < MAUD_BANDS; ++b)
        {
            r->transmission[b] = 0.0f;
        }
    }
    for (uint32_t surface = 0; surface < s->maxSurfaces && walking > 0; ++surface)
    {
        Cross(s, listener, entries, &walking);
    }
}

static bool Unit(float v)
{
    return v >= 0.0f && v <= 1.0f;
}

static bool MaterialValid(const maudAcousticMaterial* m)
{
    bool valid = Unit(m->scattering);
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        valid = valid && Unit(m->absorption[b]) && Unit(m->transmission[b]);
    }
    return valid;
}

static MAUD_NOINLINE maudResult SetMaterials(maudSpatializer* spatializer,
                                             const maudAcousticMaterial* materials, uint32_t count)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudSpatializerBusy(spatializer))
    {
        return maud_errorBusy;
    }
    if (count > 0 && materials == nullptr)
    {
        return maudSpatializerInvalid(spatializer);
    }
    if (count > spatializer->materialCapacity)
    {
        return maud_errorCapacity;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!MaterialValid(&materials[i]))
        {
            return maudSpatializerInvalid(spatializer);
        }
    }
    if (count > 0)
    {
        memcpy(spatializer->materials, materials, (size_t)count * sizeof(maudAcousticMaterial));
    }
    spatializer->materialCount = count;
    return maud_success;
}

maudResult maudSpatializer_SetMaterials(maudSpatializer* spatializer,
                                        const maudAcousticMaterial* materials, uint32_t count)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = SetMaterials(spatializer, materials, count);
    maudFloatEnvLeave(token);
    return result;
}

static MAUD_NOINLINE maudResult SimulateDirect(maudSpatializer* spatializer,
                                               const maudPose* listener)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudSpatializerBusy(spatializer))
    {
        return maud_errorBusy;
    }
    if (listener == nullptr || !maudPoseValid(listener))
    {
        return maudSpatializerInvalid(spatializer);
    }
    maudSpatializerRunHost(spatializer, true);
    Buffer* buffer = &spatializer->buffers[spatializer->back];
    for (uint32_t i = 0; i < spatializer->capacity; ++i)
    {
        const Slot* slot = &spatializer->slots[i];
        Entry* entry = &buffer->entries[i];
        if (!slot->live)
        {
            entry->generation = 0;
            continue;
        }
        entry->generation = slot->generation;
        maudDirectGeometry(listener, &slot->pose, &slot->directivity, &entry->result);
    }
    Occlude(spatializer, listener, buffer->entries);
    Transmit(spatializer, listener, buffer->entries);
    maudPathSources(spatializer, listener, buffer->entries);
    buffer->reverb = spatializer->reverb;
    buffer->step = ++spatializer->steps;
    // Publish: the written buffer waits, marked newer; the one that was
    // waiting becomes the next to write.
    uint32_t old = atomic_exchange_explicit(&spatializer->shared, spatializer->back | FRESH,
                                            memory_order_acq_rel);
    spatializer->back = old & (FRESH - 1);
    maudSpatializerRunHost(spatializer, false);
    return maud_success;
}

maudResult maudSpatializer_SimulateDirect(maudSpatializer* spatializer, const maudPose* listener)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = SimulateDirect(spatializer, listener);
    maudFloatEnvLeave(token);
    return result;
}

// Traces batches [start, end) of a reverberation estimate.
static MAUD_NOINLINE void TraceBatches(uint32_t start, uint32_t end, void* context)
{
    maudSpatializer* s = context;
    for (uint32_t b = start; b < end; ++b)
    {
        maudTraceReverbBatch(&s->trace, b, &s->histograms[b]);
    }
}

// The any-hit query for a host without one: every path clear.
static void Clear(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    (void)context;
    memset(occluded, 0, count);
}

// A reverberation step's estimate, from the baked set in use or traced.
static void EstimateReverb(maudSpatializer* s, const maudPose* listener)
{
    s->reverb.estimateCount += 1;
    if (maudBakedReverb(s, listener->position))
    {
        return;
    }
    if (s->closestHit == nullptr)
    {
        for (int b = 0; b < MAUD_BANDS; ++b)
        {
            s->reverb.reverbTime[b] = NO_REVERB;
            s->reverb.level[b] = SILENT;
            s->reverb.tailTime[b] = 0.0f;
            s->reverb.tailLevel[b] = SILENT;
        }
        s->reverb.delay = 0.0f;
        return;
    }
    uint32_t batches = maudTraceFrom(s, listener->position, &s->reverb);
    if (s->reflections != nullptr)
    {
        maudPublishReflections(s->reflections, &s->trace, s->histograms, batches);
    }
}

static MAUD_NOINLINE maudResult SimulateReverb(maudSpatializer* spatializer,
                                               const maudPose* listener)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudSpatializerBusy(spatializer))
    {
        return maud_errorBusy;
    }
    if (listener == nullptr || !maudPoseValid(listener))
    {
        return maudSpatializerInvalid(spatializer);
    }
    if (spatializer->trace.rays == 0)
    {
        return maud_errorUnsupported;
    }
    maudSpatializerRunHost(spatializer, true);
    EstimateReverb(spatializer, listener);
    maudSpatializerRunHost(spatializer, false);
    return maud_success;
}

maudResult maudSpatializer_SimulateReverb(maudSpatializer* spatializer, const maudPose* listener)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = SimulateReverb(spatializer, listener);
    maudFloatEnvLeave(token);
    return result;
}

uint32_t maudTraceFrom(maudSpatializer* s, maudVector3 position, maudReverbResult* result)
{
    s->trace.closestHit = s->closestHit;
    s->trace.anyHit = s->anyHit != nullptr ? s->anyHit : Clear;
    s->trace.context = s->rayContext;
    s->trace.materials = s->materials;
    s->trace.materialCount = s->materialCount;
    s->trace.listener = position;
    uint32_t batches = maudReverbBatches(s->trace.rays);
    maudHostTasks hooks = {s->enqueueTask, s->finishTask, s->userTaskContext};
    maudRunHostTasks(&hooks, TraceBatches, batches, s);
    maudReverbFit fit;
    maudFitReverb(s->histograms, batches, &fit);
    memcpy(result->reverbTime, fit.times, sizeof(fit.times));
    memcpy(result->tailTime, fit.tailTimes, sizeof(fit.tailTimes));
    bool hybrid = s->reflections != nullptr;
    result->delay = hybrid ? s->reflectionDuration - REVERB_ONSET : 0.0f;
    maudReverbLevels(&s->histograms[0], &fit, hybrid ? s->reflectionDuration : LEVEL_AT,
                     result->delay, result->level, result->tailLevel);
    return batches;
}

uint64_t maudSpatializer_LatchResults(maudSpatializer* spatializer)
{
    if (spatializer == nullptr)
    {
        return 0;
    }
    if ((atomic_load_explicit(&spatializer->shared, memory_order_relaxed) & FRESH) != 0)
    {
        uint32_t old = atomic_exchange_explicit(&spatializer->shared, spatializer->front,
                                                memory_order_acq_rel);
        spatializer->front = old & (FRESH - 1);
    }
    return spatializer->buffers[spatializer->front].step;
}

maudResult maudSource_GetDirectResult(const maudSpatializer* spatializer, maudSourceId source,
                                      maudDirectResult* resultOut)
{
    // Cleared first, so a failure leaves zero.
    if (resultOut != nullptr)
    {
        memset(resultOut, 0, sizeof(*resultOut));
    }
    if (spatializer == nullptr || resultOut == nullptr || source.index1 == 0 ||
        source.index1 > spatializer->capacity || source.generation == 0)
    {
        return maud_errorInvalid;
    }
    const Buffer* buffer = &spatializer->buffers[spatializer->front];
    if (buffer->step == 0)
    {
        return maud_errorInvalid;
    }
    const Entry* entry = &buffer->entries[source.index1 - 1];
    if (entry->generation != source.generation)
    {
        return maud_errorStale;
    }
    *resultOut = entry->result;
    return maud_success;
}

maudResult maudSpatializer_GetReverbResult(const maudSpatializer* spatializer,
                                           maudReverbResult* resultOut)
{
    // Cleared first, so a failure leaves zero.
    if (resultOut != nullptr)
    {
        memset(resultOut, 0, sizeof(*resultOut));
    }
    if (spatializer == nullptr || resultOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const Buffer* buffer = &spatializer->buffers[spatializer->front];
    if (buffer->step == 0)
    {
        return maud_errorInvalid;
    }
    *resultOut = buffer->reverb;
    return maud_success;
}

maudResult maudSpatializer_RenderReflections(maudSpatializer* spatializer,
                                             const maudQuaternion* orientation, const float* send,
                                             float* const* bed, uint32_t frameCount)
{
    if (spatializer == nullptr || orientation == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudQuaternion* q = orientation;
    float n = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
    if (!isfinite(n) || !(n > 0.0f))
    {
        return maudSpatializerInvalid(spatializer);
    }
    if (spatializer->reflections == nullptr)
    {
        return maud_errorUnsupported;
    }
    uint32_t channels = maudReflectionsOrder(spatializer->reflections) + 1;
    channels *= channels;
    if (frameCount > 0 && (send == nullptr || bed == nullptr))
    {
        return maudSpatializerInvalid(spatializer);
    }
    for (uint32_t c = 0; c < channels && frameCount > 0; ++c)
    {
        if (bed[c] == nullptr)
        {
            return maudSpatializerInvalid(spatializer);
        }
    }
    maudConvolveReflections(spatializer->reflections, orientation, send, bed, frameCount);
    return maud_success;
}
