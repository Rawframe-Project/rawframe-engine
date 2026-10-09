// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A 3D bake's whole input, meshes, terrains and volumes, checked as
// hostile input, for the navmesh and flight bakers.

#ifndef MAUL_NAV_SRC_BAKE_INPUT_H
#define MAUL_NAV_SRC_BAKE_INPUT_H

#include "maul-nav/bake.h"

#include <stdbool.h>
#include <stdint.h>

// Whether input's counts are at least 0 and its arrays given where its
// counts are positive.
bool mnavCheckInputArrays(const mnavBakeInput* input);

// Checks input whose arrays are checked against a def that is already
// valid: each mesh unless meshesChecked, each terrain, each volume, and
// their triangles, terrains' and volumes' points counted, against the
// def's input triangles limit. A refused element's index (terrains after
// the meshes, volumes after the terrains) goes to meshOut and its check's
// outcome to inputOut.
mnavResult mnavCheckSolidInput(const mnavBakeDef* def, const mnavBakeInput* input,
                               bool meshesChecked, int32_t* meshOut, mnavInputResult* inputOut);

#endif // MAUL_NAV_SRC_BAKE_INPUT_H
