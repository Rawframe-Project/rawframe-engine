// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A bounding volume hierarchy over triangles, built by binned SAH and
// traversed for any or closest hits. Nodes are in depth-first order: an
// inner node's left child follows it, its right child is named.

#ifndef MAUL_AUDIO_SRC_BVH_H
#define MAUL_AUDIO_SRC_BVH_H

#include "maul-audio/base.h"

#include <stdint.h>

// A triangle as the hierarchy holds it.
typedef struct maudTriangle
{
    float a[3];
    float b[3];
    float c[3];
    // Its index in the scene, the tie-breaker of equal distances.
    uint32_t index;
    uint32_t material;
} maudTriangle;

typedef struct maudBvhNode
{
    float min[3];
    // A leaf's first triangle, or an inner node's right child.
    uint32_t offset;
    float max[3];
    // A leaf's triangle count; 0 for an inner node.
    uint32_t count;
} maudBvhNode;

// The most nodes a hierarchy of count triangles takes.
uint32_t maudBvhCapacity(uint32_t count);

// Builds the hierarchy over count (at least 1) triangles, reordering
// them; nodes has maudBvhCapacity(count) room. Returns the nodes used.
uint32_t maudBuildBvh(maudTriangle* triangles, uint32_t count, maudBvhNode* nodes);

// Whether a ray hits anything within [tMin, tMax].
bool maudBvhAnyHit(const maudBvhNode* nodes, const maudTriangle* triangles, const float* origin,
                   const float* direction, float tMin, float tMax);

// The nearest hit within [tMin, tMax], ties to the lower index; NULL if
// none, else the triangle, its distance in *t.
const maudTriangle* maudBvhClosestHit(const maudBvhNode* nodes, const maudTriangle* triangles,
                                      const float* origin, const float* direction, float tMin,
                                      float tMax, float* t);

// Called for a leaf entry whose box a ray enters within the limit; may
// lower *limit, and returns true to stop the visit.
typedef bool maudBvhVisitFn(const maudTriangle* entry, float* limit, void* context);

// Visits the leaf entries whose boxes a ray enters within [tMin,
// *limit], the nearer box first, boxes entered past *limit (as visits
// lower it) skipped; returns whether a visit stopped it.
bool maudBvhVisit(const maudBvhNode* nodes, const maudTriangle* entries, const float* origin,
                  const float* direction, float tMin, float* limit, maudBvhVisitFn* visit,
                  void* context);

#endif // MAUL_AUDIO_SRC_BVH_H
