// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A flex container in its own frame of reference, shared by the layout
// algorithm (src/flex.c) and the preparation of its items (CSS Flexbox
// section 9.2): their constraints, flex base sizes, automatic minimums
// and contributions to a container sized by its content.

#ifndef MAUL_UI_SRC_FLEX_ITEM_H
#define MAUL_UI_SRC_FLEX_ITEM_H

#include "outline.h"
#include "sizing.h"
#include "solver.h"

// A container in its own frame: main and cross axes, its padding and
// border per side, its constraints and limits, and what it has resolved
// so far.
typedef struct muiFlexFrame
{
    const muiSolver* solver;
    uint32_t node;
    const muiLayoutStyle* style;
    bool row;
    bool reverse;
    float boxMainStart;
    float boxMain;
    float boxCrossStart;
    float boxCross;
    muiMeasureAxis mainIn;
    muiMeasureAxis crossIn;
    muiAxisSizing mainLimits;
    muiAxisSizing crossLimits;
    // The maximum main size its style gives, where a wrapping column
    // breaks its lines, also when its content alone is asked
    // (muiSizingInput.contentOnly) and mainLimits leave it out.
    float lineMaximum;
    // The children's percentage bases along each axis, negative when
    // indefinite: a size not given exactly, or a height that is the
    // container's content height (muiSizingInput.contentHeight).
    float extentMain;
    float extentCross;
    float gap;
    float crossGap;
    bool multiLine;
    bool wrapReverse;
    // The container's direction, which its children inherit.
    bool rtl;
    // Its height is to come from its aspect ratio (muiFlexRatioContentHeight),
    // and the extent its lines reach across.
    bool ratioHeight;
    float contentCross;
    // Some child was stretched to another size than it was measured at;
    // narrowed, to a smaller one.
    bool restretched;
    bool narrowed;
    // A wrapping column its own limits made narrower than its lines.
    bool clamped;
    // A row whose height is its content's, or definite only through
    // stretching in such rows (looseHeight): its stretched items' too.
    bool crossLoose;
    // A row whose height its style gives or was early (earlyHeight).
    bool crossEarly;
    uint32_t count;
    uint32_t lineCount;
    float innerMain;
    float innerCross;
    // Where a wrapping column with a ratio breaks its lines: the ratio's
    // inner height, before its content floor; negative otherwise.
    float ratioBreak;
} muiFlexFrame;

static inline muiSizingInput muiFlexChildInput(const muiFlexFrame* frame, muiMeasureAxis main,
                                               muiMeasureAxis cross)
{
    muiSizingInput input;
    input.width = frame->row ? main : cross;
    input.height = frame->row ? cross : main;
    input.parentWidth = frame->row ? frame->extentMain : frame->extentCross;
    input.parentHeight = frame->row ? frame->extentCross : frame->extentMain;
    input.rtl = frame->rtl;
    input.contentHeight = false;
    input.contentOnly = false;
    input.contentOnlyHeight = false;
    input.ratioPercent = false;
    input.ratioLines = false;
    input.looseHeight = frame->row && frame->crossLoose;
    input.earlyHeight = false;
    return input;
}

static inline float muiFlexMainOf(const muiFlexFrame* frame, muiSize size)
{
    return frame->row ? size.width : size.height;
}

static inline MUI_SIZE_OUTLINE float muiFlexCrossOf(const muiFlexFrame* frame, muiSize size)
{
    return frame->row ? size.height : size.width;
}

static inline MUI_SIZE_OUTLINE muiAlign muiFlexAlignOf(const muiFlexFrame* frame,
                                                       const muiLayoutStyle* child)
{
    return child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                  : frame->style->container.alignItems;
}

// Whether a child takes its line's cross size: it aligns by stretch, its
// cross size is automatic (a percentage that cannot resolve is not,
// section 9.4 step 11) and neither cross margin is automatic.
static inline bool muiFlexIsStretched(const muiFlexFrame* frame, const muiLayoutStyle* child)
{
    muiDimension cross = frame->row ? child->sizing.height : child->sizing.width;
    return muiFlexAlignOf(frame, child) == mui_alignStretch && cross.kind == mui_dimensionAuto &&
           !muiIsMarginAutoStart(child, !frame->row, frame->rtl) &&
           !muiIsMarginAutoEnd(child, !frame->row, frame->rtl);
}

// A child's padding with the safe area, in the direction it lays out in.
static inline muiEdges muiFlexChildPadding(const muiFlexFrame* frame, const muiLayoutStyle* child)
{
    return muiPaddingOf(child, &frame->solver->safeArea, muiIsRtl(child, frame->rtl));
}

// The constraint a child is sized under on the cross axis: its own
// definite size; with stretch, the line's size when the container's cross
// size is definite, not its content height, and for a container whose
// ratio gives its height the ratio's, before its content floor (as
// Chrome and Firefox); otherwise fit-content within the container.
muiMeasureAxis muiFlexCrossConstraint(const muiFlexFrame* frame, const muiLayoutStyle* child,
                                      const muiAxisSizing* cross, bool stretch);

// CSS Flexbox section 4.5: for any item but a scroll container, the
// smaller of the specified size and the min-content size, each within
// the maximum. A size the aspect ratio gives is not a specified size: the
// content wins over it, as CSS Sizing 4 says for the ratio-dependent
// axis. With an aspect ratio and a definite cross size, the min-content
// size is at least that size through the ratio (the transferred size
// suggestion), so an empty item keeps its ratio's width.
float muiFlexAutomaticMinimum(const muiFlexFrame* frame, uint32_t child, const muiAxisSizing* main,
                              muiMeasureAxis cross);

// Section 9.2: a child's margins, limits, flex base size and
// hypothetical main size. Returns its outer contribution to a container
// sized by its content: section 9.9.1's web-compatible sum, with 9.9.3's
// contributions as Chrome reads them. In a row, an item counts its
// preferred size, else its content's, capped by a given basis if it
// cannot grow and floored by it if it cannot shrink (but for a wrapping
// row's min-content size), within its limits, its automatic minimum among
// them; alone on a line, in aloneOut, its min-content contribution (a
// preferred size uncapped, content at its narrowest), as Chrome sizes a
// wrapping row. A column counts hypothetical sizes, as Chrome
// does.
// A prepared child's min-content contribution along the main axis: its
// definite main size or its content's min-content one, within its
// limits, with its margins.
float muiFlexMinContribution(const muiFlexFrame* frame, uint32_t child);

float muiFlexPrepareItem(const muiFlexFrame* frame, uint32_t child, float* aloneOut);

#endif
