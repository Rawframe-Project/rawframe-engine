// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake def: its default, its check and its conversion to cells.

#ifndef MAUL_NAV_SRC_BAKE_DEF_H
#define MAUL_NAV_SRC_BAKE_DEF_H

#include "maul-nav/bake.h"

// Checks a def and fills cells when it is valid; cells may be NULL.
mnavBakeDefResult mnavCheckBakeDef(const mnavBakeDef* def, mnavBakeCells* cells);

#endif // MAUL_NAV_SRC_BAKE_DEF_H
