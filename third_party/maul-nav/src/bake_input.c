// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A 3D bake's whole input checked: its arrays, then each mesh, terrain
// and volume, and their size against the input limit.

#include "bake_input.h"

#include "input.h"
#include "terrain.h"
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

// Notes a refused element and passes its outcome on.
static mnavResult Refused(int32_t index, mnavInputResult input, int32_t* meshOut,
                          mnavInputResult* inputOut)
{
    *meshOut = index;
    *inputOut = input;
    return input.result;
}

mnavResult mnavCheckSolidInput(const mnavBakeDef* def, const mnavBakeInput* input,
                               bool meshesChecked, int32_t* meshOut, mnavInputResult* inputOut)
{
    int64_t total = 0;
    for (int32_t m = 0; m < input->meshCount; ++m)
    {
        mnavInputResult checked = meshesChecked
                                      ? (mnavInputResult){mnav_success, mnav_elementNone, -1}
                                      : mnavCheckTriangleMesh(def, &input->meshes[m]);
        if (checked.result != mnav_success)
        {
            return Refused(m, checked, meshOut, inputOut);
        }
        total += input->meshes[m].triangleCount;
    }
    for (int32_t i = 0; i < input->terrainCount; ++i)
    {
        mnavInputResult checked = mnavCheckTerrain(def, &input->terrains[i]);
        if (checked.result != mnav_success)
        {
            return Refused(input->meshCount + i, checked, meshOut, inputOut);
        }
        total += mnavTerrainTriangles(&input->terrains[i]);
    }
    for (int32_t i = 0; i < input->volumeCount; ++i)
    {
        mnavInputResult checked = mnavCheckVolume(def, &input->volumes[i]);
        if (checked.result != mnav_success)
        {
            return Refused(input->meshCount + input->terrainCount + i, checked, meshOut, inputOut);
        }
        total += input->volumes[i].pointCount;
    }
    return total > def->limits.inputTriangles ? mnav_errorLimit : mnav_success;
}
