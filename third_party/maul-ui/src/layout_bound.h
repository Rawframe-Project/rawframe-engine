// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout bounded by the change (record mui-0003).

#ifndef MAUL_UI_SRC_LAYOUT_BOUND_H
#define MAUL_UI_SRC_LAYOUT_BOUND_H

#include "solver.h"

#include <stdbool.h>
#include <stdint.h>

// Lays out the nodes below root that layout was requested on, each in a
// subtree whose answers to its parent's queries hold, so the parent is
// not solved; true when it did, and the root's own solve then finds its
// cache. False, having laid out nothing, when the change reaches the
// root or is too wide: then every owing node's cache is to be cleared.
bool muiBoundLayout(const muiSolver* solver, uint32_t root);

#endif // MAUL_UI_SRC_LAYOUT_BOUND_H
