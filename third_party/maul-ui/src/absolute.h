// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Placing a container's absolute children by their insets in its
// padding box (record mui-0003).

#ifndef MAUL_UI_SRC_ABSOLUTE_H
#define MAUL_UI_SRC_ABSOLUTE_H

#include "solver.h"

// Sizes and places every absolute child of container, whose border box
// is size, in logical coordinates (start on the left), and lays each out
// in full through the solver; rtl is the container's direction, which
// its children inherit.
void muiPlaceAbsolute(const muiSolver* solver, uint32_t container, muiSize size, bool rtl);

#endif // MAUL_UI_SRC_ABSOLUTE_H
