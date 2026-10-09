// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A scene's instances (scene_state.h). Slots carry generations as the
// spatializer's sources do. A commit places every live instance (its
// rotation from the normalised quaternion, its inverse scale) and
// builds the top level over boxes of their meshes' bounds carried into
// the world and padded against rounding; a box is a degenerate triangle
// from its lower to its upper corner through its centre, so the
// triangles' builder serves. A ray enters an instance's frame as R^T (o
// - p) / s with its direction R^T d / s, unnormalised, so its distances
// are the world's.

#include "allocator.h"
#include "scene_state.h"

#include <math.h>
#include <string.h>

#define MIN_SCALE 0.001f
#define MAX_SCALE 1000.0f
// A world box's pad: this share of its size and of its distance from
// the origin, and this much at least.
#define PAD_SHARE 1e-5f
#define PAD_FLOOR 1e-6f

bool maudAllocateInstances(maudAcousticScene* scene, uint32_t capacity)
{
    scene->capacity = capacity;
    if (capacity == 0)
    {
        return true;
    }
    const maudAllocator* a = &scene->allocator;
    scene->instances =
        maudAllocate(a, (size_t)capacity * sizeof(maudSceneInstance), alignof(maudSceneInstance));
    scene->free = maudAllocate(a, (size_t)capacity * sizeof(uint32_t), alignof(uint32_t));
    scene->placed =
        maudAllocate(a, (size_t)capacity * sizeof(maudPlacedInstance), alignof(maudPlacedInstance));
    scene->boxes = maudAllocate(a, (size_t)capacity * sizeof(maudTriangle), alignof(maudTriangle));
    scene->top = maudAllocate(a, (size_t)maudBvhCapacity(capacity) * sizeof(maudBvhNode),
                              alignof(maudBvhNode));
    if (scene->instances == nullptr || scene->free == nullptr || scene->placed == nullptr ||
        scene->boxes == nullptr || scene->top == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < capacity; ++i)
    {
        scene->instances[i] = (maudSceneInstance){.generation = 1};
        scene->free[i] = capacity - 1 - i;
    }
    scene->freeCount = capacity;
    return true;
}

static void Free(const maudAllocator* a, void* memory, size_t bytes, size_t alignment)
{
    if (memory != nullptr)
    {
        maudRelease(a, memory, bytes, alignment);
    }
}

void maudReleaseInstances(maudAcousticScene* scene)
{
    const maudAllocator* a = &scene->allocator;
    size_t n = scene->capacity;
    Free(a, scene->instances, n * sizeof(maudSceneInstance), alignof(maudSceneInstance));
    Free(a, scene->free, n * sizeof(uint32_t), alignof(uint32_t));
    Free(a, scene->placed, n * sizeof(maudPlacedInstance), alignof(maudPlacedInstance));
    Free(a, scene->boxes, n * sizeof(maudTriangle), alignof(maudTriangle));
    Free(a, scene->top, (size_t)maudBvhCapacity(scene->capacity) * sizeof(maudBvhNode),
         alignof(maudBvhNode));
}

static bool TransformValid(const maudInstanceTransform* t)
{
    maudQuaternion q = t->orientation;
    float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return isfinite(t->position.x) && isfinite(t->position.y) && isfinite(t->position.z) &&
           isfinite(length) && length > 0.0f && t->scale >= MIN_SCALE && t->scale <= MAX_SCALE;
}

maudResult maudCreateSceneInstance(maudAcousticScene* scene, uint32_t mesh,
                                   const maudInstanceTransform* transform,
                                   maudSceneInstanceId* instanceOut)
{
    if (instanceOut != nullptr)
    {
        *instanceOut = (maudSceneInstanceId){0, 0};
    }
    if (scene == nullptr || transform == nullptr || instanceOut == nullptr ||
        mesh >= scene->meshCount || !TransformValid(transform))
    {
        return maud_errorInvalid;
    }
    if (scene->freeCount == 0)
    {
        return maud_errorCapacity;
    }
    uint32_t index = scene->free[--scene->freeCount];
    maudSceneInstance* slot = &scene->instances[index];
    slot->live = true;
    slot->mesh = mesh;
    slot->transform = *transform;
    *instanceOut = (maudSceneInstanceId){index + 1, slot->generation};
    return maud_success;
}

static maudResult Find(maudAcousticScene* scene, maudSceneInstanceId id, maudSceneInstance** out)
{
    if (scene == nullptr || id.index1 == 0 || id.index1 > scene->capacity || id.generation == 0)
    {
        return maud_errorInvalid;
    }
    maudSceneInstance* slot = &scene->instances[id.index1 - 1];
    if (!slot->live || slot->generation != id.generation)
    {
        return maud_errorStale;
    }
    *out = slot;
    return maud_success;
}

maudResult maudMoveSceneInstance(maudAcousticScene* scene, maudSceneInstanceId instance,
                                 const maudInstanceTransform* transform)
{
    maudSceneInstance* slot = nullptr;
    maudResult result = Find(scene, instance, &slot);
    if (result != maud_success)
    {
        return result;
    }
    if (transform == nullptr || !TransformValid(transform))
    {
        return maud_errorInvalid;
    }
    slot->transform = *transform;
    return maud_success;
}

maudResult maudDestroySceneInstance(maudAcousticScene* scene, maudSceneInstanceId instance)
{
    maudSceneInstance* slot = nullptr;
    maudResult result = Find(scene, instance, &slot);
    if (result != maud_success)
    {
        return result;
    }
    slot->live = false;
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    scene->free[scene->freeCount++] = instance.index1 - 1;
    return maud_success;
}

static void Place(const maudSceneInstance* slot, maudPlacedInstance* p)
{
    maudQuaternion q = slot->transform.orientation;
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    float x = q.x / n;
    float y = q.y / n;
    float z = q.z / n;
    float w = q.w / n;
    const float r[9] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w),
                        2.0f * (x * z + y * w),        2.0f * (x * y + z * w),
                        1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w),
                        2.0f * (x * z - y * w),        2.0f * (y * z + x * w),
                        1.0f - 2.0f * (x * x + y * y)};
    p->mesh = slot->mesh;
    memcpy(p->rotation, r, sizeof(r));
    p->position[0] = slot->transform.position.x;
    p->position[1] = slot->transform.position.y;
    p->position[2] = slot->transform.position.z;
    p->inverseScale = 1.0f / slot->transform.scale;
}

// The world box of a placed mesh's bounds, padded: every local point's
// world image lies inside despite rounding.
static maudTriangle WorldBox(const maudPlacedInstance* p, float scale, const maudBvhNode* root,
                             uint32_t slot)
{
    float low[3] = {INFINITY, INFINITY, INFINITY};
    float high[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (int corner = 0; corner < 8; ++corner)
    {
        float local[3] = {(corner & 1) ? root->max[0] : root->min[0],
                          (corner & 2) ? root->max[1] : root->min[1],
                          (corner & 4) ? root->max[2] : root->min[2]};
        for (int i = 0; i < 3; ++i)
        {
            float v = p->position[i] +
                      scale * (p->rotation[3 * i] * local[0] + p->rotation[3 * i + 1] * local[1] +
                               p->rotation[3 * i + 2] * local[2]);
            low[i] = fminf(low[i], v);
            high[i] = fmaxf(high[i], v);
        }
    }
    maudTriangle box = {.index = slot};
    for (int i = 0; i < 3; ++i)
    {
        float pad = PAD_SHARE * (high[i] - low[i] + fabsf(low[i]) + fabsf(high[i])) + PAD_FLOOR;
        box.a[i] = low[i] - pad;
        box.b[i] = high[i] + pad;
        box.c[i] = 0.5f * (box.a[i] + box.b[i]);
    }
    return box;
}

maudResult maudCommitAcousticScene(maudAcousticScene* scene)
{
    if (scene == nullptr)
    {
        return maud_errorInvalid;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < scene->capacity; ++i)
    {
        const maudSceneInstance* slot = &scene->instances[i];
        const maudSceneMesh* mesh = slot->live ? &scene->meshes[slot->mesh] : nullptr;
        if (mesh == nullptr || mesh->triangleCount == 0)
        {
            continue;
        }
        Place(slot, &scene->placed[i]);
        scene->boxes[n++] = WorldBox(&scene->placed[i], slot->transform.scale, &mesh->nodes[0], i);
    }
    scene->placedCount = n;
    if (n > 0)
    {
        maudBuildBvh(scene->boxes, n, scene->top);
    }
    return maud_success;
}

// A ray in an instance's frame.
typedef struct Local
{
    float origin[3];
    float direction[3];
} Local;

static Local Enter(const maudPlacedInstance* p, const float* origin, const float* direction)
{
    Local l;
    float d[3] = {origin[0] - p->position[0], origin[1] - p->position[1],
                  origin[2] - p->position[2]};
    for (int i = 0; i < 3; ++i)
    {
        const float* r = p->rotation;
        l.origin[i] = (r[i] * d[0] + r[3 + i] * d[1] + r[6 + i] * d[2]) * p->inverseScale;
        l.direction[i] = (r[i] * direction[0] + r[3 + i] * direction[1] + r[6 + i] * direction[2]) *
                         p->inverseScale;
    }
    return l;
}

typedef struct Query
{
    const maudAcousticScene* scene;
    const float* origin;
    const float* direction;
    float tMin;
    // Closest-hit: the best slot so far (UINT32_MAX for none, the static
    // scene below every slot) and the hit.
    uint32_t bestSlot;
    bool found;
    maudRayHit* hit;
} Query;

static bool AnyVisit(const maudTriangle* entry, float* limit, void* context)
{
    const Query* q = context;
    const maudPlacedInstance* p = &q->scene->placed[entry->index];
    const maudSceneMesh* mesh = &q->scene->meshes[p->mesh];
    Local l = Enter(p, q->origin, q->direction);
    return maudBvhAnyHit(mesh->nodes, mesh->triangles, l.origin, l.direction, q->tMin, *limit);
}

bool maudInstancesAnyHit(const maudAcousticScene* scene, const float* origin,
                         const float* direction, float tMin, float tMax)
{
    if (scene->placedCount == 0)
    {
        return false;
    }
    Query q = {scene, origin, direction, tMin, UINT32_MAX, false, nullptr};
    float limit = tMax;
    return maudBvhVisit(scene->top, scene->boxes, origin, direction, tMin, &limit, AnyVisit, &q);
}

static bool ClosestVisit(const maudTriangle* entry, float* limit, void* context)
{
    Query* q = context;
    uint32_t slot = entry->index;
    const maudPlacedInstance* p = &q->scene->placed[slot];
    const maudSceneMesh* mesh = &q->scene->meshes[p->mesh];
    Local l = Enter(p, q->origin, q->direction);
    float t = 0.0f;
    const maudTriangle* hit =
        maudBvhClosestHit(mesh->nodes, mesh->triangles, l.origin, l.direction, q->tMin, *limit, &t);
    if (hit != nullptr && (t < *limit || (!q->found && slot < q->bestSlot)))
    {
        maudVector3 n = maudTriangleNormal(hit);
        const float* r = p->rotation;
        double w[3];
        for (int i = 0; i < 3; ++i)
        {
            w[i] = (double)r[3 * i] * (double)n.x + (double)r[3 * i + 1] * (double)n.y +
                   (double)r[3 * i + 2] * (double)n.z;
        }
        double length = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        maudVector3 world = length > 0.0
                                ? (maudVector3){(float)(w[0] / length), (float)(w[1] / length),
                                                (float)(w[2] / length)}
                                : n;
        *q->hit = (maudRayHit){t, world, hit->material};
        *limit = t;
        q->bestSlot = slot;
        q->found = false;
    }
    return false;
}

void maudInstancesClosestHit(const maudAcousticScene* scene, const float* origin,
                             const float* direction, float tMin, bool found, float* best,
                             maudRayHit* hit)
{
    if (scene->placedCount == 0)
    {
        return;
    }
    Query q = {scene, origin, direction, tMin, UINT32_MAX, found, hit};
    maudBvhVisit(scene->top, scene->boxes, origin, direction, tMin, best, ClosestVisit, &q);
}
