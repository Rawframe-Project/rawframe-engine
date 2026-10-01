// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sizing a node: answered from its cache when it can be, measured for a
// leaf, laid out by the flex algorithm for a container, with absolute
// children placed after it (record mui-0003).

#ifndef MAUL_UI_SRC_SOLVE_H
#define MAUL_UI_SRC_SOLVE_H

#include "solver.h"

// Returns node's border-box size under input. With perform, also sets
// the rectangle of every node below it; a node whose subtree is unchanged
// and whose size is the same as last time is not revisited. The solver's
// solve member is this function.
muiSize muiSolveNode(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                     bool perform);

// The input a root is sized under in the host's space: its definite
// sizes within its limits; otherwise fit-content, which on the vertical
// axis is max-content, as for CSS's absolutely positioned boxes.
muiSizingInput muiRootInput(const muiLayoutStyle* style, float availableWidth,
                            float availableHeight);

#endif // MAUL_UI_SRC_SOLVE_H
