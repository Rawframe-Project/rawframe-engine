// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Paths over a probe set: each end attaches to the nearest probes it
// sees within the set's range, and Dijkstra's search runs from the
// source's probes to the listener's over the links, links found blocked
// left out. Ties go to the lower probe index, so a search gives the same
// path every time.

#ifndef MAUL_AUDIO_SRC_PATH_SEARCH_H
#define MAUL_AUDIO_SRC_PATH_SEARCH_H

#include "probe_graph.h"

#include <stdint.h>

// A path's vertices at most, its ends included.
#define MAUD_PATH_VERTICES 64u
// The probes an end attaches to at most, and the nearest it tests.
#define MAUD_PATH_ATTACH     8u
#define MAUD_PATH_CANDIDATES 32u
// The links a source's searches may find blocked.
#define MAUD_PATH_BLOCKED 8u

// The probes an end sees and its distances to them.
typedef struct maudPathEnd
{
    uint32_t count;
    uint32_t probes[MAUD_PATH_ATTACH];
    float lengths[MAUD_PATH_ATTACH];
} maudPathEnd;

// A search's scratch, for sets of up to capacity probes, and the links
// found blocked for the source being searched (pairs, lower index
// first).
typedef struct maudPathSearch
{
    uint32_t capacity;
    float* cost;
    uint32_t* parent;
    uint32_t* heap;
    uint32_t* position;
    // The probes whose cost a search set, to reset after it.
    uint32_t* touched;
    uint32_t touchedCount;
    uint32_t blocked[2 * MAUD_PATH_BLOCKED];
    uint32_t blockedCount;
    void* memory;
    size_t bytes;
} maudPathSearch;

// Allocates the scratch; false when memory runs out (nothing to free).
bool maudCreatePathSearch(const maudAllocator* allocator, uint32_t capacity,
                          maudPathSearch* search);
void maudDestroyPathSearch(const maudAllocator* allocator, maudPathSearch* search);

// Attaches a point: its nearest probes within range, tested by one
// query, nearest first; a NULL query sees them all.
void maudAttachPath(const maudProbeGraph* graph, float range, maudVector3 point,
                    maudAnyHitFn* anyHit, void* context, maudPathEnd* end);

// The shortest path from the source's end to the listener's: vertices
// from the listener (listener, probes, source) and the probes' indices
// (probes[k] for vertices[k + 1]). Returns the vertex count, 0 for no
// path or one past MAUD_PATH_VERTICES.
uint32_t maudSearchPath(maudPathSearch* search, const maudProbeGraph* graph,
                        const maudPathEnd* source, const maudPathEnd* listener,
                        maudVector3 sourcePoint, maudVector3 listenerPoint, maudVector3* vertices,
                        uint32_t* probes);

#endif // MAUL_AUDIO_SRC_PATH_SEARCH_H
