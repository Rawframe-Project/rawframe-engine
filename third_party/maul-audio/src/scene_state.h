// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An acoustic scene's state, shared by its files: scene.c (the static
// hierarchy, creation and the queries) and scene_instances.c (instances,
// commits and the top level).

#ifndef MAUL_AUDIO_SRC_SCENE_STATE_H
#define MAUL_AUDIO_SRC_SCENE_STATE_H

#include "bvh.h"

#include "maul-audio/scene.h"

#include <stdint.h>

// Triangles and a hierarchy over them.
typedef struct maudSceneMesh
{
    uint32_t triangleCount;
    uint32_t nodeCount;
    maudTriangle* triangles;
    maudBvhNode* nodes;
} maudSceneMesh;

// An instance as the host last set it.
typedef struct maudSceneInstance
{
    uint32_t generation;
    bool live;
    uint32_t mesh;
    maudInstanceTransform transform;
} maudSceneInstance;

// An instance as the last commit placed it: world = position + scale x
// rotation x local, the rotation's rows in order.
typedef struct maudPlacedInstance
{
    uint32_t mesh;
    float rotation[9];
    float position[3];
    float inverseScale;
} maudPlacedInstance;

struct maudAcousticScene
{
    maudAllocator allocator;
    maudSceneMesh statics;
    uint32_t meshCount;
    maudSceneMesh* meshes;
    uint32_t capacity;
    uint32_t freeCount;
    maudSceneInstance* instances;
    uint32_t* free;
    // The last commit: each live slot's placement, a box per committed
    // instance (its index the slot) and the top level over them.
    maudPlacedInstance* placed;
    uint32_t placedCount;
    maudTriangle* boxes;
    maudBvhNode* top;
};

// Copies count meshes' triangles, numbered across them, and builds
// their hierarchy; false when memory runs out (nothing to release).
bool maudBuildSceneMesh(const maudAllocator* allocator, const maudMesh* meshes, uint32_t count,
                        maudSceneMesh* out);
void maudReleaseSceneMesh(const maudAllocator* allocator, maudSceneMesh* mesh);

// A triangle's unit geometric normal as wound; 0 for a degenerate one.
maudVector3 maudTriangleNormal(const maudTriangle* t);

// The instances' slots and top level, for the def's capacity; false
// when memory runs out (maudReleaseInstances frees what was made).
bool maudAllocateInstances(maudAcousticScene* scene, uint32_t capacity);
void maudReleaseInstances(maudAcousticScene* scene);

// Whether a ray hits a committed instance within [tMin, tMax].
bool maudInstancesAnyHit(const maudAcousticScene* scene, const float* origin,
                         const float* direction, float tMin, float tMax);

// A committed instance's hit nearer than *best, or at it when the
// static scene has none there (found false) and the instance's slot is
// the lowest at that distance; writes *best and *hit.
void maudInstancesClosestHit(const maudAcousticScene* scene, const float* origin,
                             const float* direction, float tMin, bool found, float* best,
                             maudRayHit* hit);

#endif // MAUL_AUDIO_SRC_SCENE_STATE_H
