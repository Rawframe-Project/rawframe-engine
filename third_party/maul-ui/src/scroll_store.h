// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scroll state (record mui-0007): per node, its offset and the extent its
// children reach, logical (x from the inline start), which layout
// measures and src/scroll.c moves.

#ifndef MAUL_UI_SRC_SCROLL_STORE_H
#define MAUL_UI_SRC_SCROLL_STORE_H

#include "layout_node.h"

#include "maul-ui/scroll.h"

typedef struct muiScrollState
{
    float x;
    float y;
    // From the padding box's start, at least the padding box.
    float extentWidth;
    float extentHeight;
    // How far a pan past a limit, rubber banded, moves the children on
    // beyond it along each axis, logical as the offsets: drawn and hit,
    // not an offset.
    float overX;
    float overY;
    // The length a virtual list's items need along each axis, from the
    // content box's start (src/virtual.c): the extent is at least that.
    float listX;
    float listY;
    // How far its children and padding box reach, from the padding box's
    // start, as layout last measured them (src/solve.c): the extent is
    // that or the list's length, whichever is further.
    float reachWidth;
    float reachHeight;
} muiScrollState;

// The default rule (muiDefaultScrollRule).
#define MUI_SCROLL_RULE                                                                            \
    ((muiScrollRule){.wheelStep = 100.0f,                                                          \
                     .lineStep = 40.0f,                                                            \
                     .pageFraction = 0.875f,                                                       \
                     .latchNs = 500000000ull,                                                      \
                     .easeNs = 150000000ull,                                                       \
                     .decelerationRate = 0.998f,                                                   \
                     .overscroll = false})

// The most scroll containers easing a step at once; a step past them
// jumps.
#define MUI_SCROLL_EASES 8

// What moves a scroll container over time.
typedef enum muiScrollEaseKind
{
    // Offsets easing out from where they were to where the step goes.
    muiScrollEaseStep = 0,
    // Offsets from where the fling began at its velocity, decaying.
    muiScrollEaseFling = 1,
    // An overscroll springing back to none from where it was.
    muiScrollEaseBounce = 2,
} muiScrollEaseKind;

typedef struct muiScrollEase
{
    muiNodeId node;
    float fromX;
    float fromY;
    float toX;
    float toY;
    uint64_t startNs;
    muiScrollEaseKind kind;
    float velocityX;
    float velocityY;
} muiScrollEase;

// The most pans going at once, and the moves each keeps for its
// velocity.
#define MUI_SCROLL_PANS    4
#define MUI_SCROLL_SAMPLES 8

typedef struct muiScrollSample
{
    uint64_t timeNs;
    float x;
    float y;
} muiScrollSample;

// A touch dragging a scroll container: its offsets when the drag began
// and its latest moves, a ring.
typedef struct muiScrollPan
{
    uint32_t pointer;
    muiNodeId node;
    float startX;
    float startY;
    uint32_t count;
    muiScrollSample samples[MUI_SCROLL_SAMPLES];
} muiScrollPan;

// The rule, the scroll container the wheel last scrolled with when, and
// the steps easing.
typedef struct muiScrollStore
{
    muiScrollRule rule;
    muiNodeId latched;
    uint64_t latchedNs;
    uint32_t easeCount;
    muiScrollEase eases[MUI_SCROLL_EASES];
    uint32_t panCount;
    muiScrollPan pans[MUI_SCROLL_PANS];
} muiScrollStore;

static inline void muiScrollInit(muiScrollStore* store)
{
    *store = (muiScrollStore){.rule = MUI_SCROLL_RULE};
}

// The furthest an offset goes along an axis: the extent less the padding
// box, of a node of a style and border box size, for an axis it scrolls,
// else 0.
static inline float muiScrollLimit(const muiLayoutStyle* style, muiSize size,
                                   const muiScrollState* scroll, bool horizontal)
{
    muiScrollAxes axis = horizontal ? mui_scrollHorizontal : mui_scrollVertical;
    if ((style->scrollAxes & axis) == 0)
    {
        return 0.0f;
    }
    float box = horizontal ? size.width - style->border.start - style->border.end
                           : size.height - style->border.top - style->border.bottom;
    float extent = horizontal ? scroll->extentWidth : scroll->extentHeight;
    return extent > box ? extent - box : 0.0f;
}

// Drops a node's offset and extent when it stops scrolling: they live
// with the scroll container, as a browser's do. (Along an axis it still
// scrolls but no longer along, layout's limit of 0 drops the offset.) The
// layout style change repaints the node, which sets the transforms anew.
static inline void muiSyncScroll(muiScrollState* scroll, muiScrollAxes axes)
{
    if (axes == mui_scrollNone)
    {
        *scroll = (muiScrollState){0};
    }
}

// How far a node moves its children on the surface: by its offsets and
// its overscroll, the logical x leftward under right to left; nothing for
// a node that does not scroll.
static inline float muiScrollShiftX(const muiLayoutNode* node, const muiScrollState* scroll)
{
    if (node->style.scrollAxes == mui_scrollNone)
    {
        return 0.0f;
    }
    float x = scroll->x + scroll->overX;
    return node->rtl ? x : -x;
}

static inline float muiScrollShiftY(const muiLayoutNode* node, const muiScrollState* scroll)
{
    return node->style.scrollAxes == mui_scrollNone ? 0.0f : -(scroll->y + scroll->overY);
}

#endif // MAUL_UI_SRC_SCROLL_STORE_H
