// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flex algorithm for a container's in-flow children (record
// mui-0003).

#ifndef MAUL_UI_SRC_FLEX_H
#define MAUL_UI_SRC_FLEX_H

#include "solver.h"

// Returns a container's border-box size under input, its children sized
// through the solver; with perform, also sets the rectangles of its
// in-flow children and lays each out in full.
muiSize muiLayoutFlex(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                      bool perform);

// A container's first baseline laid out at input, exact on both axes,
// from the top of its border box; NaN when it has no flow children.
float muiFlexBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input);

// A container's content height under input, exact at the height its
// aspect ratio gives: a single-line row's stretched items will take that
// height (Flexbox 9.8), so of them only their minimums count.
float muiFlexRatioContentHeight(const muiSolver* solver, uint32_t node,
                                const muiSizingInput* input);

#endif // MAUL_UI_SRC_FLEX_H
