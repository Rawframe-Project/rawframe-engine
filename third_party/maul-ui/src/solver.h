// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What one muiComputeLayout works with. Layout algorithms reach their
// children through solve, so that the modules that size a node and the
// algorithms that call back into it do not include each other.

#ifndef MAUL_UI_SRC_SOLVER_H
#define MAUL_UI_SRC_SOLVER_H

#include "draw_store.h"
#include "layout_node.h"
#include "tree.h"

typedef struct muiSolver muiSolver;

// Returns node's border-box size under input; with perform, also lays out
// its subtree.
typedef muiSize (*muiSolveFunction)(const muiSolver* solver, uint32_t node,
                                    const muiSizingInput* input, bool perform);

// Returns node's first baseline laid out at input, exact on both axes:
// its distance down from the top of its border box, which is its bottom
// when it has none, as CSS synthesizes one.
typedef float (*muiBaselineSolveFunction)(const muiSolver* solver, uint32_t node,
                                          const muiSizingInput* input);

struct muiSolver
{
    const muiTree* tree;
    muiLayoutNode* nodes;
    muiMeasureFunction measure;
    void* measureUser;
    muiBaselineFunction measureBaseline;
    muiSolveFunction solve;
    muiBaselineSolveFunction baseline;
    // Where the solver requests style for the next pass, on nodes whose
    // conditions read a size or direction their layout changed, and paint
    // on nodes whose rectangle or direction is not what was last painted.
    muiTree* restyle;
    // What each node was last painted as, parallel to the tree's slots.
    const muiPaintState* painted;
};

#endif // MAUL_UI_SRC_SOLVER_H
