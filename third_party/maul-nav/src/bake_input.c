// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A bake's whole input checked: its arrays, then each mesh, terrain and
// volume, or each outline, and their size against the input limit.

#include "bake_input.h"

#include "bake_def.h"
#include "fpenv.h"
#include "input.h"
#include "terrain.h"
#include "tile_index.h"
#include "volume.h"

#include "maul-nav/bake.h"

#include <stdbool.h>
#include <stdint.h>

bool mnavCheckInputArrays(const mnavBakeInput* input)
{
    return input->meshCount >= 0 && (input->meshCount == 0 || input->meshes != nullptr) &&
           input->terrainCount >= 0 && (input->terrainCount == 0 || input->terrains != nullptr) &&
           input->volumeCount >= 0 && (input->volumeCount == 0 || input->volumes != nullptr);
}

// Notes a refused input and passes its outcome on.
static mnavResult Refused(mnavInputResult checked, mnavInputKind kind, int32_t index,
                          mnavInputResult* inputOut)
{
    *inputOut = mnavInputNamed(checked, kind, index);
    return checked.result;
}

mnavResult mnavCheckSolidInput(const mnavBakeDef* def, const mnavBakeInput* input,
                               bool meshesChecked, mnavInputResult* inputOut)
{
    int64_t total = 0;
    for (int32_t m = 0; m < input->meshCount; ++m)
    {
        mnavInputResult checked = meshesChecked
                                      ? mnavInputElementResult(mnav_success, mnav_elementNone, -1)
                                      : mnavCheckTriangleMesh(def, &input->meshes[m]);
        if (checked.result != mnav_success)
        {
            return Refused(checked, mnav_inputMesh, m, inputOut);
        }
        total += input->meshes[m].triangleCount;
    }
    for (int32_t i = 0; i < input->terrainCount; ++i)
    {
        mnavInputResult checked = mnavCheckTerrain(def, &input->terrains[i]);
        if (checked.result != mnav_success)
        {
            return Refused(checked, mnav_inputTerrain, i, inputOut);
        }
        total += mnavTerrainTriangles(&input->terrains[i]);
    }
    for (int32_t i = 0; i < input->volumeCount; ++i)
    {
        mnavInputResult checked = mnavCheckVolume(def, &input->volumes[i]);
        if (checked.result != mnav_success)
        {
            return Refused(checked, mnav_inputVolume, i, inputOut);
        }
        total += input->volumes[i].pointCount;
    }
    return total > def->limits.inputTriangles ? mnav_errorCapacity : mnav_success;
}

// The outcome of a whole input's check that refuses no one input.
static mnavInputResult Whole(mnavResult result)
{
    return mnavInputElementResult(result, mnav_elementNone, -1);
}

static MNAV_NOINLINE mnavInputResult ValidateBakeInputWork(const mnavBakeDef* def,
                                                           const mnavBakeInput* input)
{
    if (def == nullptr || input == nullptr)
    {
        return Whole(mnav_errorInvalid);
    }
    mnavBakeCells cells = {0};
    mnavResult checked = mnavCheckBakeDef(def, &cells).result;
    if (checked != mnav_success)
    {
        return Whole(checked);
    }
    if (!mnavCheckInputArrays(input) ||
        (input->index != nullptr &&
         !mnavTileIndexFits(input->index, def, &cells, input->meshes, input->meshCount)))
    {
        return Whole(mnav_errorInvalid);
    }
    mnavInputResult refused = Whole(mnav_success);
    mnavResult result = mnavCheckSolidInput(def, input, false, &refused);
    return refused.result != mnav_success ? refused : Whole(result);
}

mnavInputResult mnavValidateBakeInput(const mnavBakeDef* def, const mnavBakeInput* input)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavInputResult result = ValidateBakeInputWork(def, input);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavInputResult ValidateBake2DInputWork(const mnavBakeDef* def,
                                                             const mnavBake2DInput* input)
{
    if (def == nullptr || input == nullptr)
    {
        return Whole(mnav_errorInvalid);
    }
    mnavBakeCells cells = {0};
    mnavResult checked = mnavCheckBakeDef(def, &cells).result;
    if (checked != mnav_success)
    {
        return Whole(checked);
    }
    const mnavOutline* outlines = input->outlines;
    int32_t outlineCount = input->outlineCount;
    if (outlineCount < 0 || (outlineCount > 0 && outlines == nullptr) ||
        (input->index != nullptr &&
         !mnavTileIndexFits2D(input->index, def, &cells, outlines, outlineCount)))
    {
        return Whole(mnav_errorInvalid);
    }
    int64_t total = 0;
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        mnavInputResult refused = mnavCheckOutline(def, &outlines[o]);
        if (refused.result != mnav_success)
        {
            return mnavInputNamed(refused, mnav_inputOutline, o);
        }
        total += outlines[o].pointCount;
    }
    return Whole(total > def->limits.inputTriangles ? mnav_errorCapacity : mnav_success);
}

mnavInputResult mnavValidateBake2DInput(const mnavBakeDef* def, const mnavBake2DInput* input)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavInputResult result = ValidateBake2DInputWork(def, input);
    mnavFloatEnvLeave(env);
    return result;
}
