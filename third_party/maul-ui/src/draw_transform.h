// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The values of a list's transforms (records mui-0005, mui-0007).

#ifndef MAUL_UI_SRC_DRAW_TRANSFORM_H
#define MAUL_UI_SRC_DRAW_TRANSFORM_H

#include "context.h"
#include "draw_store.h"

#include <stdint.h>

// The origin of a node's parent: its ancestors' places added up to the
// list's root, in doubles as hit testing adds them.
void muiParentOrigin(const muiContext* context, uint32_t root, uint32_t node, float* xOut,
                     float* yOut);

// A transform's value after its parent entry's: with MUI_TRANSFORM_SCALE
// in owner, the owner's scale about its origin where layout puts it; else
// a scroll container's translation by its offset, logical x leftward
// under right to left, rounded to device pixels at scale so that snapped
// edges stay snapped, through the parent's scale.
muiDrawTransform muiTransformAfter(const muiContext* context, uint32_t root, uint32_t owner,
                                   const muiDrawTransform* parent, float scale);

// Gives each of a list's transforms its value, after its parent entry's.
void muiSetTransforms(const muiContext* context, uint32_t root, muiDrawTables* tables, float scale);

// The value one of a list's transforms has from its owner and its parent
// entries', before the list's values are given. Transforms only scale,
// by 0 or more, and move: b and c are 0.
muiDrawTransform muiTransformOf(const muiContext* context, uint32_t root,
                                const muiDrawTables* tables, uint32_t index, float scale);

#endif // MAUL_UI_SRC_DRAW_TRANSFORM_H
