// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scenes (maul-audio/scene.h): the static meshes' triangles
// copied in order, numbered across the meshes, and a hierarchy over
// them; each instancing mesh likewise on its own; the instances in
// scene_instances.c. A query asks the static hierarchy first, then the
// committed instances, which win a tie only against nothing.

#include "maul-audio/scene.h"

#include "allocator.h"
#include "bvh.h"
#include "fpenv.h"
#include "scene_state.h"

#include <math.h>

static bool MeshValid(const maudMesh* mesh)
{
    if (mesh->triangleCount == 0)
    {
        return true;
    }
    if (mesh->vertices == nullptr || mesh->indices == nullptr || mesh->materials == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < mesh->vertexCount; ++i)
    {
        maudVector3 v = mesh->vertices[i];
        if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z))
        {
            return false;
        }
    }
    for (uint64_t i = 0; i < 3 * (uint64_t)mesh->triangleCount; ++i)
    {
        if (mesh->indices[i] >= mesh->vertexCount)
        {
            return false;
        }
    }
    return true;
}

static bool MeshesValid(const maudMesh* meshes, uint32_t count, uint64_t* sum)
{
    if (count > 0 && meshes == nullptr)
    {
        return false;
    }
    for (uint32_t m = 0; m < count; ++m)
    {
        if (!MeshValid(&meshes[m]))
        {
            return false;
        }
        *sum += meshes[m].triangleCount;
    }
    return true;
}

// Checks the def and its triangles against its limit.
static maudResult Check(const maudSceneDef* def)
{
    uint64_t sum = 0;
    if (def->cookie == MAUD_SCENE_DEF_COOKIE &&
        (def->instanceCapacity > MAUD_MAX_INSTANCES || def->maxTriangles > MAUD_MAX_TRIANGLES))
    {
        return maud_errorLimit;
    }
    if (def->cookie != MAUD_SCENE_DEF_COOKIE || !maudIsAllocatorValid(&def->allocator) ||
        def->maxTriangles == 0 || !MeshesValid(def->meshes, def->meshCount, &sum) ||
        !MeshesValid(def->instanceMeshes, def->instanceMeshCount, &sum))
    {
        return maud_errorInvalid;
    }
    return sum > def->maxTriangles ? maud_errorCapacity : maud_success;
}

bool maudBuildSceneMesh(const maudAllocator* allocator, const maudMesh* meshes, uint32_t count,
                        maudSceneMesh* out)
{
    *out = (maudSceneMesh){0};
    uint32_t total = 0;
    for (uint32_t m = 0; m < count; ++m)
    {
        total += meshes[m].triangleCount;
    }
    if (total == 0)
    {
        return true;
    }
    out->triangles =
        maudAllocate(allocator, (size_t)total * sizeof(maudTriangle), alignof(maudTriangle));
    out->nodes = maudAllocate(allocator, (size_t)maudBvhCapacity(total) * sizeof(maudBvhNode),
                              alignof(maudBvhNode));
    out->triangleCount = total;
    if (out->triangles == nullptr || out->nodes == nullptr)
    {
        maudReleaseSceneMesh(allocator, out);
        return false;
    }
    uint32_t n = 0;
    for (uint32_t m = 0; m < count; ++m)
    {
        const maudMesh* mesh = &meshes[m];
        for (uint32_t i = 0; i < mesh->triangleCount; ++i, ++n)
        {
            const maudVector3* v = mesh->vertices;
            maudVector3 a = v[mesh->indices[3 * (size_t)i]];
            maudVector3 b = v[mesh->indices[3 * (size_t)i + 1]];
            maudVector3 c = v[mesh->indices[3 * (size_t)i + 2]];
            out->triangles[n] = (maudTriangle){
                {a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, n, mesh->materials[i]};
        }
    }
    out->nodeCount = maudBuildBvh(out->triangles, total, out->nodes);
    return true;
}

void maudReleaseSceneMesh(const maudAllocator* allocator, maudSceneMesh* mesh)
{
    if (mesh->triangles != nullptr)
    {
        maudRelease(allocator, mesh->triangles, (size_t)mesh->triangleCount * sizeof(maudTriangle),
                    alignof(maudTriangle));
    }
    if (mesh->nodes != nullptr)
    {
        maudRelease(allocator, mesh->nodes,
                    (size_t)maudBvhCapacity(mesh->triangleCount) * sizeof(maudBvhNode),
                    alignof(maudBvhNode));
    }
    *mesh = (maudSceneMesh){0};
}

static void Release(maudScene* scene)
{
    maudAllocator allocator = scene->allocator;
    maudReleaseSceneMesh(&allocator, &scene->statics);
    if (scene->meshes != nullptr)
    {
        for (uint32_t m = 0; m < scene->meshCount; ++m)
        {
            maudReleaseSceneMesh(&allocator, &scene->meshes[m]);
        }
        maudRelease(&allocator, scene->meshes, (size_t)scene->meshCount * sizeof(maudSceneMesh),
                    alignof(maudSceneMesh));
    }
    maudReleaseInstances(scene);
    maudRelease(&allocator, scene, sizeof(maudScene), alignof(maudScene));
}

// Builds every hierarchy and the instances' slots; false when memory
// runs out.
static bool Build(maudScene* scene, const maudSceneDef* def)
{
    if (!maudBuildSceneMesh(&def->allocator, def->meshes, def->meshCount, &scene->statics))
    {
        return false;
    }
    if (def->instanceMeshCount > 0)
    {
        scene->meshes =
            maudAllocate(&def->allocator, (size_t)def->instanceMeshCount * sizeof(maudSceneMesh),
                         alignof(maudSceneMesh));
        if (scene->meshes == nullptr)
        {
            return false;
        }
        scene->meshCount = def->instanceMeshCount;
        for (uint32_t m = 0; m < def->instanceMeshCount; ++m)
        {
            scene->meshes[m] = (maudSceneMesh){0};
        }
        for (uint32_t m = 0; m < def->instanceMeshCount; ++m)
        {
            if (!maudBuildSceneMesh(&def->allocator, &def->instanceMeshes[m], 1, &scene->meshes[m]))
            {
                return false;
            }
        }
    }
    return maudAllocateInstances(scene, def->instanceCapacity);
}

static MAUD_NOINLINE maudResult CreateAcousticScene(const maudSceneDef* def, maudScene** sceneOut)
{
    if (sceneOut != nullptr)
    {
        *sceneOut = nullptr;
    }
    if (def != nullptr && def->cookie == MAUD_SCENE_DEF_COOKIE && def->version != MAUD_ABI_VERSION)
    {
        return maud_errorVersion;
    }
    if (def == nullptr || sceneOut == nullptr)
    {
        return maud_errorInvalid;
    }
    maudResult result = Check(def);
    if (result != maud_success)
    {
        return result;
    }
    maudScene* scene = maudAllocate(&def->allocator, sizeof(maudScene), alignof(maudScene));
    if (scene == nullptr)
    {
        return maud_errorCapacity;
    }
    *scene = (maudScene){.allocator = def->allocator};
    atomic_init(&scene->misuse, 0);
    if (!Build(scene, def))
    {
        Release(scene);
        return maud_errorCapacity;
    }
    *sceneOut = scene;
    return maud_success;
}

maudResult maudCreateScene(const maudSceneDef* def, maudScene** sceneOut)
{
    uint64_t token = maudFloatEnvEnter();
    maudResult result = CreateAcousticScene(def, sceneOut);
    maudFloatEnvLeave(token);
    return result;
}

void maudDestroyScene(maudScene* scene)
{
    if (scene != nullptr)
    {
        Release(scene);
    }
}

uint64_t maudScene_GetMisuse(const maudScene* scene)
{
    return atomic_load_explicit(&scene->misuse, memory_order_relaxed);
}

static void Unpack(const maudRay* ray, float* origin, float* direction)
{
    origin[0] = ray->origin.x;
    origin[1] = ray->origin.y;
    origin[2] = ray->origin.z;
    direction[0] = ray->direction.x;
    direction[1] = ray->direction.y;
    direction[2] = ray->direction.z;
}

static MAUD_NOINLINE void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded,
                                 void* scene)
{
    const maudScene* s = scene;
    for (uint32_t i = 0; i < count; ++i)
    {
        float origin[3];
        float direction[3];
        Unpack(&rays[i], origin, direction);
        float tMin = rays[i].minDistance;
        float tMax = rays[i].maxDistance;
        occluded[i] =
            (s->statics.triangleCount > 0 && maudBvhAnyHit(s->statics.nodes, s->statics.triangles,
                                                           origin, direction, tMin, tMax)) ||
            maudInstancesAnyHit(s, origin, direction, tMin, tMax);
    }
}

// The queries enter too: a host may call them from its own threads, and
// inside the spatializer's tasks entering costs two reads of the register.
void maudFindAnyHit(const maudRay* rays, uint32_t count, uint8_t* occludedOut, void* scene)
{
    uint64_t token = maudFloatEnvEnter();
    AnyHit(rays, count, occludedOut, scene);
    maudFloatEnvLeave(token);
}

maudVector3 maudTriangleNormal(const maudTriangle* t)
{
    double u[3];
    double v[3];
    for (int i = 0; i < 3; ++i)
    {
        u[i] = (double)t->b[i] - (double)t->a[i];
        v[i] = (double)t->c[i] - (double)t->a[i];
    }
    double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length == 0.0)
    {
        return (maudVector3){0.0f, 0.0f, 0.0f};
    }
    return (maudVector3){(float)(n[0] / length), (float)(n[1] / length), (float)(n[2] / length)};
}

static MAUD_NOINLINE void ClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits,
                                     void* scene)
{
    const maudScene* s = scene;
    for (uint32_t i = 0; i < count; ++i)
    {
        hits[i] = (maudRayHit){INFINITY, {0.0f, 0.0f, 0.0f}, 0};
        float origin[3];
        float direction[3];
        Unpack(&rays[i], origin, direction);
        float best = rays[i].maxDistance;
        bool found = false;
        if (s->statics.triangleCount > 0)
        {
            float t = 0.0f;
            const maudTriangle* hit =
                maudBvhClosestHit(s->statics.nodes, s->statics.triangles, origin, direction,
                                  rays[i].minDistance, rays[i].maxDistance, &t);
            if (hit != nullptr)
            {
                hits[i] = (maudRayHit){t, maudTriangleNormal(hit), hit->material};
                best = t;
                found = true;
            }
        }
        maudInstancesClosestHit(s, origin, direction, rays[i].minDistance, found, &best, &hits[i]);
    }
}

void maudFindClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hitsOut, void* scene)
{
    uint64_t token = maudFloatEnvEnter();
    ClosestHit(rays, count, hitsOut, scene);
    maudFloatEnvLeave(token);
}
