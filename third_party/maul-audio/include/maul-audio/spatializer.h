// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Spatial part's root. A spatializer has two sides. The simulation
// side, called from the host's own threads or tasks, makes sources, sets
// their poses and runs steps that work out what the direct path does to
// each source. The rendering side, on the audio thread, latches the
// newest step and reads each source's result without allocating, locking
// or waiting; a step is published whole, never torn. The host passes the
// results to the direct effect and to the binaural effect or a panner.
//
// Positions are in metres in the host's world, right-handed with +y up;
// an orientation turns the frame of the listener or a source (+x right,
// +y up, -z ahead) into the world's.

#ifndef MAUL_AUDIO_SPATIALIZER_H
#define MAUL_AUDIO_SPATIALIZER_H

#include "maul-audio/base.h"
#include "maul-audio/direct.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A spatializer.
    typedef struct maudSpatializer maudSpatializer;

    // A source of a spatializer; 0 is no source.
    typedef struct maudSourceId
    {
        uint32_t index1;
        uint32_t generation;
    } maudSourceId;

    // A probe set of a spatializer; 0 is no set.
    typedef struct maudProbeSetId
    {
        uint32_t index1;
        uint32_t generation;
    } maudProbeSetId;

    // Where something is and which way it faces.
    typedef struct maudPose
    {
        maudVector3 position;
        // Turns the thing's frame into the world's; any nonzero length is
        // normalized.
        maudQuaternion orientation;
    } maudPose;

    // How a source's occlusion is found.
    typedef uint8_t maudOcclusionMethod;
    enum
    {
        // Not at all: the path is clear.
        maud_occlusionNone = 0,
        // By one ray from the listener to the source: 0 or 1.
        maud_occlusionRay = 1,
        // By points in a sphere around the source: of the points the
        // source sees, the share the listener does not.
        maud_occlusionVolumetric = 2,
    };

    // A ray the spatializer asks about: from origin along a unit
    // direction, between minDistance and maxDistance.
    typedef struct maudRay
    {
        maudVector3 origin;
        maudVector3 direction;
        float minDistance;
        float maxDistance;
    } maudRay;

    // Answers whether each of count rays hits anything within its
    // distances, writing 1 or 0 to occluded[i]. A ray's answer must
    // depend only on the ray and the host's geometry. Called from inside
    // a step, on its thread or on the host's tasks, never on the audio
    // thread, with at most 64 rays at a time.
    typedef void maudAnyHitFn(const maudRay* rays, uint32_t count, uint8_t* occluded,
                              void* context);

    // A surface's acoustic properties per band, each 0 to 1.
    typedef struct maudAcousticMaterial
    {
        // The share of energy a reflection off the surface absorbs.
        float absorption[MAUD_DIRECT_BANDS];
        // The share of a reflection scattered rather than mirrored.
        float scattering;
        // The share of amplitude a crossing of the surface lets through. A
        // closed wall has two surfaces, and its material each one's share.
        float transmission[MAUD_DIRECT_BANDS];
    } maudAcousticMaterial;

    // What a closest-hit query found on a ray: the distance to the
    // nearest hit within the ray's distances (any value outside them,
    // INFINITY for instance, for none), the surface's normal and its
    // material's index in the spatializer's table.
    typedef struct maudRayHit
    {
        float distance;
        maudVector3 normal;
        uint32_t material;
    } maudRayHit;

    // Finds each of count rays' nearest hit, writing hits[i]; as
    // maudAnyHitFn, an answer depends only on the ray and the geometry.
    typedef void maudClosestHitFn(const maudRay* rays, uint32_t count, maudRayHit* hits,
                                  void* context);

    // The task hooks: enqueueTask has the host run task over [0,
    // itemCount) in ranges of at least minRange items, each exactly once,
    // on any threads in any order, and returns a handle that finishTask
    // waits on. Results do not depend on the split.
    typedef void maudTaskFn(uint32_t start, uint32_t end, void* taskContext);
    typedef void* maudEnqueueTaskFn(maudTaskFn* task, uint32_t itemCount, uint32_t minRange,
                                    void* taskContext, void* userContext);
    typedef void maudFinishTaskFn(void* userTask, void* userContext);

    // How to create a spatializer. Build it with
    // maudDefaultSpatializerDef.
    typedef struct maudSpatializerDef
    {
        uint32_t cookie;
        // The most sources at once, 1 to 65,536.
        uint32_t sourceCapacity;
        // The most occlusion points a volumetric source may have, 1 to
        // 1,024.
        uint32_t maxOcclusionSamples;
        // The host's any-hit query; NULL leaves every path clear.
        maudAnyHitFn* anyHit;
        // The host's closest-hit query; NULL lets every occluded path
        // pass nothing.
        maudClosestHitFn* closestHit;
        void* rayContext;
        // The most surfaces a transmission path counts, 1 to 16.
        uint32_t maxSurfaces;
        // The material table's capacity, 1 to 65,536.
        uint32_t materialCapacity;
        // The rays a reverberation estimate traces, 64 to 16,384, or 0
        // for no estimates. Diffuse rooms are within 2 % at 1024; a
        // smooth room with one absorbent surface needs about 8000.
        uint32_t reverbRays;
        // The air's amplitude exponent per metre and band, for the
        // estimate (as the direct effect's def).
        float airAbsorption[MAUD_DIRECT_BANDS];
        // Geometric reflections: the bed's order (0 for none, else 1 to
        // 3; they need reverbRays), the response's length in seconds
        // (0.05 to 4) and the rate rendering runs at (44,100 to
        // 384,000 Hz). Memory grows with all three: four responses of
        // (order + 1)^2 channels.
        uint32_t reflectionOrder;
        float reflectionDuration;
        float reflectionRate;
        // Probe sets for pathing: how many at once (0 to 64), the most
        // probes one holds (1 to 65,536) and the most probe pairs within
        // range one tests (1 to 16,777,216). A set takes its memory when
        // made: 12 bytes a probe and 16 a link, with 9 bytes a pair and
        // 5 a column while it is built.
        uint32_t probeSetCapacity;
        uint32_t maxProbes;
        uint32_t maxProbePairs;
        // The most occluded sources a direct step looks for paths for,
        // in slot order (0 to 4,096; 0 for no pathing): 780 bytes each.
        uint32_t maxPaths;
        // The task hooks; both NULL runs queries on the step's thread.
        maudEnqueueTaskFn* enqueueTask;
        maudFinishTaskFn* finishTask;
        void* userTaskContext;
        maudAllocator allocator;
    } maudSpatializerDef;

    // How to create a source. Build it with maudDefaultSourceDef.
    typedef struct maudSourceDef
    {
        uint32_t cookie;
        // The source's directivity.
        maudDirectivityPattern directivity;
        // How its occlusion is found.
        maudOcclusionMethod occlusion;
        // For volumetric occlusion: the sphere's radius in metres, above
        // 0, and its points, 1 to the spatializer's maxOcclusionSamples.
        float occlusionRadius;
        uint32_t occlusionSamples;
        // Whether an occluded path's transmission is walked through the
        // closest-hit query.
        bool transmission;
        // Whether an occluded source looks for a path around what hides
        // it, over the probe set in use (maudSetPathing).
        bool pathing;
    } maudSourceDef;

    // How to create a probe set: probes at the host's points or, without
    // them, generated; two probes within range are linked when one ray
    // connects them. Build it with maudDefaultProbeSetDef.
    typedef struct maudProbeSetDef
    {
        uint32_t cookie;
        // The host's probes (finite), or NULL to generate them; with
        // points the box is not used.
        const maudVector3* points;
        uint32_t pointCount;
        // Generation: columns on a grid of this spacing (0.25 to 100 m)
        // over the box's x and z, centred, each walked down from the
        // box's top by the closest-hit query; a probe goes at height
        // (0.1 to 10 m) above each horizontal surface (its normal within
        // 45 degrees of vertical) with that height free above it, at
        // most 8 a column. y is up. A slab thicker than the height may
        // hold a probe inside it; give such scenes points.
        maudVector3 boxMin;
        maudVector3 boxMax;
        float spacing;
        float height;
        // The longest link, 0.1 to 1,000 m.
        float range;
    } maudProbeSetDef;

    // What a probe set holds.
    typedef struct maudProbeSetInfo
    {
        uint32_t probes;
        uint32_t links;
        // Whether it holds a bake.
        bool baked;
    } maudProbeSetInfo;

    // What a step found for a source.
    typedef struct maudDirectResult
    {
        // From the listener to the source, in metres.
        float distance;
        // The direction from the listener to the source in the listener's
        // frame, unit length (straight ahead when the two coincide).
        maudVector3 direction;
        // The source's directivity towards the listener, per band.
        float directivity[MAUD_DIRECT_BANDS];
        // How much of the source the path's obstacles hide, 0 to 1.
        float occlusion;
        // What passes through them, per band: the product of the crossed
        // surfaces' transmission. 1 for a clear path; 0 for an occluded
        // one without a walk.
        float transmission[MAUD_DIRECT_BANDS];
        // The surfaces the walk crossed; the spatializer's maxSurfaces
        // when the limit stopped it, more surfaces perhaps uncounted.
        uint32_t surfaces;
        // Whether a path around the obstacles was found. Then distance is
        // the path's length; direction and directivity blend the path's
        // with the straight line's by the energy each brings; and
        // transmission per band is the energy sum of what diffracts
        // around the path's corners and what passes through, at most 1.
        // The host's attenuation and air absorption, taken from
        // distance, then cover the longer way.
        bool pathed;
    } maudDirectResult;

    // A reverberation estimate as a step published it.
    typedef struct maudReverbResult
    {
        // The time to decay by 60 dB per band, 0.1 to 20 s, for
        // maudReverbParams; 0.1 s before the first estimate.
        float reverbTime[MAUD_DIRECT_BANDS];
        // The estimates made before the step; 0 for none.
        uint32_t estimates;
        // For maudReverbParams: the send's level per band (dB) and delay
        // (s) that give the reverb the traced energy. Without reflections
        // it matches at 100 ms with no delay; with them, the tail begins
        // where the reflections' response ends, at its level (the
        // reverb's first 23 ms are silent, so the delay is the duration
        // less 23 ms). 0 dB and no delay before an estimate, -96 dB with
        // nothing to reflect.
        float level[MAUD_DIRECT_BANDS];
        float delay;
        // Where a band decays in two slopes, as a room fed through a door
        // by a livelier one does, the slower slope: its time (0.1 to 20 s;
        // 0 where the band has one slope) and its level for the same send
        // (dB, -96 where there is none); reverbTime and level are then the
        // faster slope's. maudReverbParams' tailTime and tailLevel take
        // them, for a reverb made with a tail.
        float tailTime[MAUD_DIRECT_BANDS];
        float tailLevel[MAUD_DIRECT_BANDS];
    } maudReverbResult;

    /// Returns the default spatializer def: 256 sources, up to 64
    /// occlusion points each, transmission paths of up to 4 surfaces, 64
    /// materials, reverberation estimates of 2048 rays through air at
    /// 20 degrees and 50 % humidity, no geometric reflections (when
    /// asked for: 1 s at 48 kHz), no ray queries, no task hooks.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpatializerDef maudDefaultSpatializerDef(void);

    /// Returns the default source def: omnidirectional, occlusion by one
    /// ray (for volumetric occlusion a sphere of 1 m with 32 points),
    /// transmission walked.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSourceDef maudDefaultSourceDef(void);

    /// Creates a spatializer, allocating everything it will use.
    ///
    /// @param def             The def, from maudDefaultSpatializerDef.
    /// @param spatializerOut  Receives the spatializer; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a capacity or ray count out of
    ///         range, an air absorption below 0 or not finite, a
    ///         reflection order, duration or rate out of range (or
    ///         reflections without reverberation rays), or one task hook
    ///         without the other; `maud_errorCapacity` when the
    ///         allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSpatializer(const maudSpatializerDef* def,
                                                             maudSpatializer** spatializerOut);

    /// Destroys a spatializer and its sources. NULL is ignored. Neither
    /// side may be in use.
    ///
    /// @param spatializer  The spatializer.
    /// @par Thread safety
    /// Safe from any thread; the spatializer is used by one thread at a
    /// time.
    MAUD_API void maudDestroySpatializer(maudSpatializer* spatializer);

    /// Creates a source, at the world's origin facing -z until its pose
    /// is set. It appears in results from the next step on.
    ///
    /// @param spatializer  The spatializer.
    /// @param def          The def, from maudDefaultSourceDef.
    /// @param sourceOut    Receives the source; 0 on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, an invalid directivity pattern, an
    ///         unknown occlusion method, or a volumetric radius or point
    ///         count out of range;
    ///         `maud_errorCapacity` when the spatializer has its capacity
    ///         of sources.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSource(maudSpatializer* spatializer,
                                                        const maudSourceDef* def,
                                                        maudSourceId* sourceOut);

    /// Destroys a source. Results from steps before this still hold it;
    /// later steps do not.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL spatializer
    ///         or a 0 or unknown id, or `maud_errorStale` for a destroyed
    ///         source's id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudDestroySource(maudSpatializer* spatializer,
                                                         maudSourceId source);

    /// Sets a source's pose for the steps that follow.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @param pose         The pose.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL pointer, a 0
    ///         or unknown id, a value that is not finite or a zero
    ///         orientation, or `maud_errorStale` for a destroyed source's
    ///         id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetSourcePose(maudSpatializer* spatializer,
                                                         maudSourceId source, const maudPose* pose);

    /// Runs a direct step for a listener and publishes its results. Its
    /// occlusion rays go to the any-hit query, and then the transmission
    /// walks of the sources it found occluded to the closest-hit query, a
    /// surface at a time, in a fixed order and in batches of at most 64,
    /// through the task hooks if set; no result depends on how the tasks
    /// split the work.
    ///
    /// @param spatializer  The spatializer.
    /// @param listener     The listener's pose.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a
    ///         value that is not finite or a zero orientation.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSimulateDirect(maudSpatializer* spatializer,
                                                          const maudPose* listener);

    /// Replaces the material table that hits' indices name. A hit on a
    /// material outside it lets nothing through.
    ///
    /// @param spatializer  The spatializer.
    /// @param materials    count materials; NULL if count is 0.
    /// @param count        Up to the def's materialCapacity.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         value outside 0 to 1; `maud_errorCapacity` for more than the
    ///         capacity. Nothing changes on failure.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetMaterials(maudSpatializer* spatializer,
                                                        const maudAcousticMaterial* materials,
                                                        uint32_t count);

    /// Estimates the reverberation times at the listener's position from
    /// the scene: rays from the listener in batches of 64, each hit lit
    /// back toward it through the any-hit query, energy per band in 10 ms
    /// bins, a fit of the decay from -5 to -25 dB. The batches go through
    /// the task hooks if set; the times do not depend on how the tasks
    /// split them. Without a closest-hit query there is nothing to
    /// reflect and the times are 0.1 s. The next direct step publishes
    /// the times with its results. If the def asks for reflections, the
    /// same rays' energy, on spherical harmonics of its arrival
    /// direction, becomes a response (noise shaped per 10 ms and per
    /// band) published at once for maudRenderReflections.
    ///
    /// @param spatializer  The spatializer.
    /// @param listener     The listener's pose.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         value that is not finite or a zero orientation;
    ///         `maud_errorState` when the def asked for no estimates.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSimulateReverb(maudSpatializer* spatializer,
                                                          const maudPose* listener);

    /// Latches the newest published step for the rendering side; until
    /// the next latch, results come from it.
    ///
    /// @param spatializer  The spatializer.
    /// @return The step's number (the first step is 1), or 0 if none has
    ///         been published or spatializer is NULL.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_API uint64_t maudLatchResults(maudSpatializer* spatializer);

    /// Reads a source's result from the latched step.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @param resultOut    Receives the result.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a 0
    ///         or unknown id, or no latched step; `maud_errorStale` when
    ///         the latched step does not hold the source (made after it,
    ///         or destroyed before it).
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDirectResult(const maudSpatializer* spatializer,
                                                           maudSourceId source,
                                                           maudDirectResult* resultOut);

    /// Renders geometric reflections: convolves the host's mono send with
    /// the newest response published, crossfading over 128 frames to a
    /// new one, and adds the result into a bed of the def's order (ACN,
    /// SN3D) in the listener's frame. The response is advanced by the
    /// convolution's 128-frame block, so the output does not lag.
    ///
    /// @param spatializer  The spatializer.
    /// @param orientation  The listener's orientation at the call's end;
    ///                     the bed turns linearly from the last call's.
    /// @param send         frames samples.
    /// @param bed          (order + 1)^2 channels of frames samples, added
    ///                     to.
    /// @param frames       The frames; 0 does nothing.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or
    ///         an orientation of zero length or not finite;
    ///         `maud_errorState` when the def asked for no reflections.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRenderReflections(maudSpatializer* spatializer,
                                                             const maudQuaternion* orientation,
                                                             const float* send, float* const* bed,
                                                             uint32_t frames);

    /// Reads the reverberation estimate the latched step published.
    ///
    /// @param spatializer  The spatializer.
    /// @param resultOut    Receives the result.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         no latched step.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetReverbResult(const maudSpatializer* spatializer,
                                                           maudReverbResult* resultOut);

    /// Returns the default probe set def: no points and an empty box (set
    /// one or the other), 2 m spacing, 1.5 m height, a 5 m range.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudProbeSetDef maudDefaultProbeSetDef(void);

    /// Creates a probe set: the def's points, or probes generated over
    /// its box; then the links, a ray for each pair within range (every
    /// pair is linked without an any-hit query). The rays go through the
    /// task hooks; the same scene and def give the same set however they
    /// run.
    ///
    /// @param spatializer  The spatializer.
    /// @param def          The def, from maudDefaultProbeSetDef.
    /// @param setOut       Receives the set; 0 on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a value out of range or not
    ///         finite, or neither points nor a box (one with height);
    ///         `maud_errorState` to generate without a closest-hit query;
    ///         `maud_errorCapacity` when the spatializer has its capacity
    ///         of sets, past maxProbes or maxProbePairs, past 4,194,304
    ///         columns, or when memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudCreateProbeSet(maudSpatializer* spatializer,
                                                          const maudProbeSetDef* def,
                                                          maudProbeSetId* setOut);

    /// Destroys a probe set.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL spatializer
    ///         or a 0 or unknown id, or `maud_errorStale` for a destroyed
    ///         set's id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudDestroyProbeSet(maudSpatializer* spatializer,
                                                           maudProbeSetId set);

    /// Reads what a probe set holds and, if points is not NULL, copies
    /// its probes from first on into points, count of them.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set.
    /// @param infoOut      Receives the counts.
    /// @param first        The first probe to copy.
    /// @param count        The probes to copy.
    /// @param points       Receives them, or NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL spatializer
    ///         or info, a 0 or unknown id, or probes past the set's;
    ///         `maud_errorStale` for a destroyed set's id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetProbeSet(const maudSpatializer* spatializer,
                                                       maudProbeSetId set,
                                                       maudProbeSetInfo* infoOut, uint32_t first,
                                                       uint32_t count, maudVector3* points);

    /// Sets the probe set occluded sources look for paths over, from the
    /// next direct step on; 0 for none. Destroying the set ends its use.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set, or 0.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL spatializer
    ///         or an unknown id; `maud_errorStale` for a destroyed set's
    ///         id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetPathing(maudSpatializer* spatializer,
                                                      maudProbeSetId set);

    /// Bakes a probe set: a reverberation estimate at every probe, as
    /// maudSimulateReverb makes one there, with the energy field when the
    /// spatializer renders reflections; it replaces an earlier bake. The
    /// rays go through the task hooks, and the bake's values are the same
    /// bits on every platform and however the tasks run. Its memory: 24
    /// bytes a probe, and with reflections 12 times the bed's channels
    /// times the response's 10 ms bins in bytes more.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL spatializer
    ///         or a 0 or unknown id; `maud_errorStale` for a destroyed
    ///         set's id; `maud_errorState` without reverberation rays or
    ///         a closest-hit query; `maud_errorCapacity` when memory runs
    ///         out (an earlier bake is then gone).
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudBakeProbeSet(maudSpatializer* spatializer,
                                                        maudProbeSetId set);

    /// Sets the baked probe set reverberation estimates come from, from
    /// the next maudSimulateReverb on; 0 for none. An estimate then blends
    /// the 4 nearest probes the listener sees within the set's range
    /// (Shepard's weights, cut off at the fifth nearest, so moving is
    /// smooth) and traces nothing; with no probe in sight it traces.
    /// Destroying the set ends its use.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set, or 0.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL spatializer
    ///         or an unknown id; `maud_errorStale` for a destroyed set's
    ///         id; `maud_errorState` for a set without a bake.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudUseBakedReverb(maudSpatializer* spatializer,
                                                          maudProbeSetId set);

    /// Saves a probe set as a `.maudbake` file (docs/bake-format.md):
    /// its probes, links and bake. A set saves to the same bytes on every
    /// platform.
    ///
    /// @param spatializer  The spatializer.
    /// @param set          The set.
    /// @param bytes        Receives the file, or NULL to ask its size.
    /// @param capacity     The bytes bytes holds.
    /// @param sizeOut      Receives the file's size.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL spatializer
    ///         or size, or a 0 or unknown id; `maud_errorStale` for a
    ///         destroyed set's id; `maud_errorCapacity` when capacity is
    ///         short of the size (nothing written) or the size does not
    ///         fit in size_t.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSaveProbeSet(const maudSpatializer* spatializer,
                                                        maudProbeSetId set, void* bytes,
                                                        size_t capacity, size_t* sizeOut);

    /// Loads a probe set from a `.maudbake` file, treated as hostile:
    /// every count is checked against the spatializer's limits before
    /// anything is allocated, and every value against its range. A file
    /// with a bake loads only if its fields match the spatializer's
    /// reflections (none, or the same order and length).
    ///
    /// @param spatializer  The spatializer.
    /// @param bytes        The file.
    /// @param size         Its size in bytes.
    /// @param setOut       Receives the set; 0 on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or
    ///         a file not well formed; `maud_errorUnsupported` for
    ///         another version or fields that do not match;
    ///         `maud_errorCapacity` past maxProbes or maxProbePairs, with
    ///         the spatializer's sets all taken or none asked for, or when
    ///         memory runs out.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudLoadProbeSet(maudSpatializer* spatializer,
                                                        const void* bytes, size_t size,
                                                        maudProbeSetId* setOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_SPATIALIZER_H
