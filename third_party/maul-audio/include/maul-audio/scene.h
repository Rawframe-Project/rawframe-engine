// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scenes: the library's own geometry and ray tracer, for hosts
// without one and for work whose results must match on every platform.
// A scene's static meshes are fixed when it is made; instances of its
// instancing meshes are made, moved and destroyed at any time and take
// effect at a commit. Between commits any number of threads query it at
// once; changes and commits must not overlap queries. Its query functions have the
// spatializer's hook signatures, the scene as their context.

#ifndef MAUL_AUDIO_SCENE_H
#define MAUL_AUDIO_SCENE_H

#include "maul-audio/base.h"
#include "maul-audio/spatializer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // An acoustic scene.
    typedef struct maudAcousticScene maudAcousticScene;

    // Triangles over shared vertices, in the world's coordinates.
    typedef struct maudMesh
    {
        const maudVector3* vertices;
        uint32_t vertexCount;
        // Three vertex indices per triangle, counterclockwise seen from
        // the triangle's front.
        const uint32_t* indices;
        // Each triangle's index in the spatializer's material table.
        const uint32_t* materials;
        uint32_t triangleCount;
    } maudMesh;

    // An instance in a scene; 0 is no instance.
    typedef struct maudSceneInstanceId
    {
        uint32_t index1;
        uint32_t generation;
    } maudSceneInstanceId;

    // Where an instance is: a local point p lands at position + scale x
    // (p turned by orientation), the orientation a quaternion of any
    // nonzero length, scale 0.001 to 1,000.
    typedef struct maudInstanceTransform
    {
        maudVector3 position;
        maudQuaternion orientation;
        float scale;
    } maudInstanceTransform;

    // How to create an acoustic scene. Build it with
    // maudDefaultAcousticSceneDef.
    typedef struct maudAcousticSceneDef
    {
        uint32_t cookie;
        // The static meshes, copied at creation.
        const maudMesh* meshes;
        uint32_t meshCount;
        // The meshes instances are made of, in their own coordinates,
        // copied and built at creation.
        const maudMesh* instanceMeshes;
        uint32_t instanceMeshCount;
        // The most instances at once, 0 to 65,536.
        uint32_t instanceCapacity;
        // The most triangles over all meshes, static and instancing, 1 to
        // 16,777,216.
        uint32_t maxTriangles;
        maudAllocator allocator;
    } maudAcousticSceneDef;

    /// Returns the default acoustic scene def: no meshes, no instances,
    /// 16,777,216 triangles at most.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudAcousticSceneDef maudDefaultAcousticSceneDef(void);

    /// Creates an acoustic scene: copies the meshes and builds a bounding
    /// volume hierarchy over their triangles, the same on every platform.
    ///
    /// @param def       The def, from maudDefaultAcousticSceneDef.
    /// @param sceneOut  Receives the scene; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a mesh with a NULL array it needs, a
    ///         vertex index past its vertices or a vertex that is not
    ///         finite; `maud_errorCapacity` for more than 16,777,216
    ///         triangles in all or when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateAcousticScene(const maudAcousticSceneDef* def,
                                                               maudAcousticScene** sceneOut);

    /// Destroys an acoustic scene. NULL is ignored. No query may be
    /// running on it.
    ///
    /// @param scene  The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudDestroyAcousticScene(maudAcousticScene* scene);

    /// Answers any-hit queries against a scene (a maudAnyHitFn): a ray
    /// hits when a triangle lies within [minDistance, maxDistance] along
    /// it. A ray through an edge or vertex shared by triangles hits.
    ///
    /// @param rays      count rays.
    /// @param count     The rays.
    /// @param occluded  Receives 1 or 0 per ray.
    /// @param scene     The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudSceneAnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded,
                                  void* scene);

    /// Answers closest-hit queries against a scene (a maudClosestHitFn):
    /// the nearest triangle within a ray's distances, equal distances
    /// going to the triangle listed first (meshes in order, then their
    /// triangles); its unit normal faces the side the triangle is wound
    /// counterclockwise from. A miss has distance INFINITY.
    ///
    /// @param rays   count rays.
    /// @param count  The rays.
    /// @param hits   Receives a hit per ray.
    /// @param scene  The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudSceneClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits,
                                      void* scene);

    /// Makes an instance of one of the scene's instancing meshes. It is
    /// part of queries from the next commit on.
    ///
    /// @param scene        The scene.
    /// @param mesh         The instancing mesh's index in the def.
    /// @param transform    Where it is.
    /// @param instanceOut  Receives the instance; 0 on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         mesh out of range or a transform out of range or not
    ///         finite; `maud_errorCapacity` when the scene has its
    ///         capacity of instances.
    /// @par Thread safety
    /// Safe from any thread; the scene is used by one thread at a time.
    /// Changes and commits must not overlap its queries.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSceneInstance(
        maudAcousticScene* scene, uint32_t mesh, const maudInstanceTransform* transform,
        maudSceneInstanceId* instanceOut);

    /// Moves an instance, from the next commit on.
    ///
    /// @param scene      The scene.
    /// @param instance   The instance.
    /// @param transform  Where it is now.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a 0
    ///         or unknown id or a transform out of range or not finite;
    ///         `maud_errorStale` for a destroyed instance's id.
    /// @par Thread safety
    /// Safe from any thread; the scene is used by one thread at a time.
    /// Changes and commits must not overlap its queries.
    MAUD_NODISCARD MAUD_API maudResult
    maudMoveSceneInstance(maudAcousticScene* scene, maudSceneInstanceId instance,
                          const maudInstanceTransform* transform);

    /// Destroys an instance; it leaves queries at the next commit.
    ///
    /// @param scene     The scene.
    /// @param instance  The instance.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL scene or a 0
    ///         or unknown id, or `maud_errorStale` for a destroyed
    ///         instance's id.
    /// @par Thread safety
    /// Safe from any thread; the scene is used by one thread at a time.
    /// Changes and commits must not overlap its queries.
    MAUD_NODISCARD MAUD_API maudResult maudDestroySceneInstance(maudAcousticScene* scene,
                                                                maudSceneInstanceId instance);

    /// Commits the instances' changes: the live instances, where they
    /// are now, are what queries see from here on. The top level is
    /// rebuilt over their boxes, the same on every platform.
    ///
    /// @param scene  The scene.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL scene.
    /// @par Thread safety
    /// Safe from any thread; the scene is used by one thread at a time.
    /// Changes and commits must not overlap its queries.
    MAUD_NODISCARD MAUD_API maudResult maudCommitAcousticScene(maudAcousticScene* scene);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_SCENE_H
