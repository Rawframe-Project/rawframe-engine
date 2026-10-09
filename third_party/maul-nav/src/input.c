// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake's input surface, checked as hostile input.

#include "input.h"

#include "bake_def.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdbool.h>

static mnavInputResult Refuse(mnavResult result, mnavInputElement element, int32_t index)
{
    return (mnavInputResult){result, element, index};
}

static mnavInputResult CheckVertex(const mnavBakeDef* def, const mnavTriangleMesh* mesh, int32_t i)
{
    // Multiplying by a power of two is exact; the vertical bound rounds
    // once, the same way everywhere.
    float ground = def->cellSize * (float)MNAV_MAX_EXTENT_CELLS;
    float vertical = def->cellHeight * (float)MNAV_MAX_HEIGHT_CELLS;
    mnavVec3 v = mesh->vertices[i];
    if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z))
    {
        return Refuse(mnav_errorInvalid, mnav_elementVertex, i);
    }
    if (fabsf(v.x) > ground || fabsf(v.z) > ground || fabsf(v.y) > vertical)
    {
        return Refuse(mnav_errorRange, mnav_elementVertex, i);
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

static mnavInputResult CheckVertices(const mnavBakeDef* def, const mnavTriangleMesh* mesh)
{
    for (int32_t i = 0; i < mesh->vertexCount; ++i)
    {
        mnavInputResult result = CheckVertex(def, mesh, i);
        if (result.result != mnav_success)
        {
            return result;
        }
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

// Checks triangle t's corners and area, not its vertices.
static mnavInputResult CheckTriangle(const mnavTriangleMesh* mesh, int32_t t)
{
    const int32_t* corner = mesh->indices + (size_t)t * 3;
    for (int k = 0; k < 3; ++k)
    {
        if (corner[k] < 0 || corner[k] >= mesh->vertexCount)
        {
            return Refuse(mnav_errorInvalid, mnav_elementTriangle, t);
        }
    }
    if (mesh->areas != nullptr && mesh->areas[t] >= MNAV_AREA_TYPES)
    {
        return Refuse(mnav_errorInvalid, mnav_elementTriangle, t);
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

static mnavInputResult CheckTriangles(const mnavTriangleMesh* mesh)
{
    for (int32_t t = 0; t < mesh->triangleCount; ++t)
    {
        mnavInputResult result = CheckTriangle(mesh, t);
        if (result.result != mnav_success)
        {
            return result;
        }
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

mnavInputResult mnavCheckMeshShape(const mnavBakeDef* def, const mnavTriangleMesh* mesh)
{
    if (mesh->vertexCount < 0 || mesh->triangleCount < 0 ||
        (mesh->vertexCount > 0 && mesh->vertices == nullptr) ||
        (mesh->triangleCount > 0 && mesh->indices == nullptr))
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    if (mesh->triangleCount > def->limits.inputTriangles)
    {
        return Refuse(mnav_errorLimit, mnav_elementNone, -1);
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

mnavInputResult mnavCheckMeshTriangle(const mnavBakeDef* def, const mnavTriangleMesh* mesh,
                                      int32_t t)
{
    mnavInputResult result = CheckTriangle(mesh, t);
    const int32_t* corner = mesh->indices + (size_t)t * 3;
    for (int k = 0; k < 3 && result.result == mnav_success; ++k)
    {
        result = CheckVertex(def, mesh, corner[k]);
    }
    return result;
}

mnavInputResult mnavCheckTriangleMesh(const mnavBakeDef* def, const mnavTriangleMesh* mesh)
{
    mnavInputResult result = mnavCheckMeshShape(def, mesh);
    if (result.result != mnav_success)
    {
        return result;
    }
    result = CheckVertices(def, mesh);
    if (result.result != mnav_success)
    {
        return result;
    }
    return CheckTriangles(mesh);
}

mnavInputResult mnavValidateTriangleMesh(const mnavBakeDef* def, const mnavTriangleMesh* mesh)
{
    if (def == nullptr || mesh == nullptr || mnavCheckBakeDef(def, nullptr).result != mnav_success)
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    return mnavCheckTriangleMesh(def, mesh);
}

mnavInputResult mnavCheckOutline(const mnavBakeDef* def, const mnavOutline* outline)
{
    if (outline->pointCount < 3 || outline->points == nullptr || outline->area >= MNAV_AREA_TYPES)
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    if (outline->pointCount > def->limits.inputTriangles)
    {
        return Refuse(mnav_errorLimit, mnav_elementNone, -1);
    }
    float ground = def->cellSize * (float)MNAV_MAX_EXTENT_CELLS;
    for (int32_t i = 0; i < outline->pointCount; ++i)
    {
        mnavVec2 p = outline->points[i];
        if (!isfinite(p.x) || !isfinite(p.y))
        {
            return Refuse(mnav_errorInvalid, mnav_elementPoint, i);
        }
        if (fabsf(p.x) > ground || fabsf(p.y) > ground)
        {
            return Refuse(mnav_errorRange, mnav_elementPoint, i);
        }
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

mnavInputResult mnavValidateOutline(const mnavBakeDef* def, const mnavOutline* outline)
{
    if (def == nullptr || outline == nullptr ||
        mnavCheckBakeDef(def, nullptr).result != mnav_success)
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    return mnavCheckOutline(def, outline);
}
