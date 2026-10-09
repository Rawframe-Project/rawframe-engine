// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pathing in a direct step: for each occluded source that asks, a path
// over the active probe set, searched on the step's thread (links found
// blocked by a ray are left out and the search runs again, three times
// at most), then refined and fed into the source's direct result over
// the host's tasks. A path makes the result's distance its length, its
// direction and the source's directivity an energy-weighted blend of
// the path's and the straight one's, and its transmission per band the
// energy sum of what diffracts around and what passes through (raised
// by the straight path's shorter spread), at most 1.

#ifndef MAUL_AUDIO_SRC_PATHING_H
#define MAUL_AUDIO_SRC_PATHING_H

#include "path_refine.h"
#include "path_search.h"
#include "probe_graph.h"

#include <stdint.h>

// A source this step: where it is, how it radiates, its result (the
// straight path's values in, the path's out).
typedef struct maudPathJob
{
    const maudPose* pose;
    const maudDirectivityPattern* pattern;
    maudDirectResult* result;
} maudPathJob;

typedef struct maudPathing maudPathing;

// Pathing for sets of up to maxProbes probes and up to maxPaths sources
// a step; NULL when memory runs out.
maudPathing* maudCreatePathing(const maudAllocator* allocator, uint32_t maxProbes,
                               uint32_t maxPaths);
void maudDestroyPathing(maudPathing* pathing);

// The jobs' array, maxPaths long, for the step to fill.
maudPathJob* maudPathJobs(maudPathing* pathing);

// Runs count jobs (at most maxPaths) over a set.
void maudRunPathing(maudPathing* pathing, const maudProbeGraph* graph,
                    const maudProbeQueries* queries, const maudPose* listener, uint32_t count);

#endif // MAUL_AUDIO_SRC_PATHING_H
