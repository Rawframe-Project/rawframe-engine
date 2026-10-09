// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Probe sets: points and the links between them. Probes are the host's
// or are generated down columns of a grid over a box, a probe at a
// height above every horizontal surface with that height free above
// it; two probes within a range are linked when one ray connects them.
// Rays go out in batches of 64 over the host's tasks, and every result
// lands in a fixed order, so the same scene and def give the same set
// however the batches run.

#ifndef MAUL_AUDIO_SRC_PROBE_GRAPH_H
#define MAUL_AUDIO_SRC_PROBE_GRAPH_H

#include "maul-audio/spatializer.h"

#include <stdint.h>

#define MAUD_PROBE_BATCH 64u
// The floors a column finds at most, and the surfaces it crosses.
#define MAUD_COLUMN_FLOORS 8u
#define MAUD_COLUMN_HITS   32u
// The most columns a generation walks.
#define MAUD_MAX_COLUMNS 16777216u

// What building a set uses: the host's queries and tasks, the
// spatializer's allocator and limits.
typedef struct maudProbeQueries
{
    maudAnyHitFn* anyHit;
    maudClosestHitFn* closestHit;
    void* rayContext;
    maudEnqueueTaskFn* enqueueTask;
    maudFinishTaskFn* finishTask;
    void* userTaskContext;
    const maudAllocator* allocator;
    uint32_t maxProbes;
    uint32_t maxPairs;
} maudProbeQueries;

// A set: its probes, and its links in compressed rows: probe i's
// neighbours (ascending) and their distances are entries offsets[i] to
// offsets[i + 1].
typedef struct maudProbeGraph
{
    uint32_t count;
    uint32_t links;
    // The longest link, from the def.
    float range;
    maudVector3* points;
    uint32_t* offsets;
    uint32_t* neighbours;
    float* lengths;
    // The block holding all of it.
    void* memory;
    size_t bytes;
} maudProbeGraph;

// Whether a def's values are in range (points finite, a valid box).
bool maudProbeSetDefValid(const maudProbeSetDef* def);

// Builds a set for a valid def. maud_errorState when it asks for
// generation without a closest-hit query; maud_errorCapacity past the
// limits or the columns' cap, or when memory runs out. The graph is
// zeroed on failure.
maudResult maudBuildProbeGraph(const maudProbeQueries* queries, const maudProbeSetDef* def,
                               maudProbeGraph* graph);

// Allocates a set's block for count probes and links, setting its
// arrays, counts and size (not its values); false when memory runs out.
bool maudLayProbeGraph(const maudAllocator* allocator, maudProbeGraph* graph, uint32_t count,
                       uint32_t links);

// Releases a built set and zeroes it.
void maudReleaseProbeGraph(const maudAllocator* allocator, maudProbeGraph* graph);

#endif // MAUL_AUDIO_SRC_PROBE_GRAPH_H
