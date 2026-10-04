// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake's input surface, checked as hostile input.

#ifndef MAUL_NAV_SRC_INPUT_H
#define MAUL_NAV_SRC_INPUT_H

#include "maul-nav/bake.h"

// Checks a mesh against a def that is already valid.
mnavInputResult mnavCheckTriangleMesh(const mnavBakeDef* def, const mnavTriangleMesh* mesh);

// Checks an outline against a def that is already valid.
mnavInputResult mnavCheckOutline(const mnavBakeDef* def, const mnavOutline* outline);

#endif // MAUL_NAV_SRC_INPUT_H
