// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spatializer's probe sets: slots with generations, as its sources
// have, each holding a built set (probe_graph.h).

#ifndef MAUL_AUDIO_SRC_PROBE_SETS_H
#define MAUL_AUDIO_SRC_PROBE_SETS_H

#include "probe_bake.h"
#include "probe_graph.h"

#include <stdint.h>

typedef struct maudProbeSets maudProbeSets;

// Slots for capacity sets (1 or more); NULL when memory runs out.
maudProbeSets* maudCreateProbeSets(const maudAllocator* allocator, uint32_t capacity);

// Releases the slots and every set in them.
void maudDestroyProbeSets(maudProbeSets* sets);

// Builds a set for a valid def into a free slot; maud_errorCapacity
// with none free, else what building returns.
maudResult maudAddProbeSet(maudProbeSets* sets, const maudProbeQueries* queries,
                           const maudProbeSetDef* def, maudProbeSetId* setOut);

// Takes a graph and bake made elsewhere into a free slot, owning them
// from then on; maud_errorCapacity with none free (they stay the
// caller's).
maudResult maudAdoptProbeSet(maudProbeSets* sets, maudProbeGraph* graph, maudProbeBake* bake,
                             maudProbeSetId* setOut);

// Releases a set: maud_errorInvalid for a 0 or unknown id,
// maud_errorStale for a released one.
maudResult maudRemoveProbeSet(maudProbeSets* sets, maudProbeSetId set);

// Finds a set's graph and bake (count 0 when it has none), with the
// errors of maudRemoveProbeSet.
maudResult maudFindProbeBake(maudProbeSets* sets, maudProbeSetId set,
                             const maudProbeGraph** graphOut, maudProbeBake** bakeOut);

// Finds a set, with the errors of maudRemoveProbeSet.
maudResult maudFindProbeSet(const maudProbeSets* sets, maudProbeSetId set,
                            const maudProbeGraph** graphOut);

#endif // MAUL_AUDIO_SRC_PROBE_SETS_H
