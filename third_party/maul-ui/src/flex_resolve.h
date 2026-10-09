// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parts of the flex algorithm that only arrange numbers already
// known: resolving flexible lengths (CSS Flexbox section 9.7) and
// spacing a line along its main axis.

#ifndef MAUL_UI_SRC_FLEX_RESOLVE_H
#define MAUL_UI_SRC_FLEX_RESOLVE_H

#include "layout_node.h"
#include "tree.h"

// The number of children, from first on, that form one line: as many as
// fit innerMain with gap between them, at least one; every remaining one
// when wrap is false. Sizes are the outer hypothetical main sizes.
uint32_t muiCollectLine(const muiTree* tree, const muiLayoutNode* nodes, uint32_t first,
                        float innerMain, float gap, bool wrap);

// Sets the target main size of count children from first on, one line,
// from their bases, hypothetical sizes, limits and flex factors, so the
// line fills innerMain where it can. gaps is the space between them.
void muiResolveFlexibleLengths(const muiTree* tree, muiLayoutNode* nodes, uint32_t first,
                               uint32_t count, float innerMain, float gaps);

// The space before the first of count items and the extra space between
// two of them, for free space left on the line (negative when the items
// overflow); reversed when the main axis starts at the writing mode's end.
void muiJustifySpacing(muiJustify justify, float freeSpace, uint32_t count, bool reversed,
                       float* leadOut, float* betweenOut);

// The space before the first of count lines, the extra space between two
// of them, and what each line grows by, for free cross space left
// (negative when the lines overflow); reversed when the cross axis starts
// at the writing mode's end.
void muiAlignContentSpacing(muiAlignContent align, float freeSpace, uint32_t count, bool reversed,
                            float* leadOut, float* betweenOut, float* growOut);

#endif // MAUL_UI_SRC_FLEX_RESOLVE_H
