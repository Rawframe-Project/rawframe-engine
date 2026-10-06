// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): what input, focus and layout ask of it.

#ifndef MAUL_UI_SRC_SCROLL_H
#define MAUL_UI_SRC_SCROLL_H

#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/pointer.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// The default of an unhandled wheel turn over hit (muiWheelInput), under
// root: whether a scroll container took it.
bool muiScrollWheel(muiContext* context, uint32_t root, muiNodeId hit, const muiWheelEvent* event);

// The nearest node from at up that scrolls along an axis, not past stop,
// a focus scope (the layer holding the focus, or the root); 0 for none.
uint32_t muiScrollerOf(const muiContext* context, uint32_t stop, uint32_t at, bool horizontal);

// Whether a node lies within half a scrollport of a scrolling ancestor's
// visible part along an axis, as Android's ScrollView asks before moving
// the focus to it.
bool muiScrollIsNear(const muiContext* context, uint32_t container, uint32_t node, bool horizontal);

// Steps a scroll container a line in a direction; whether it could move.
bool muiScrollLine(muiContext* context, uint32_t container, muiDirection direction,
                   uint64_t timeNs);

// Steps a scroll container vertically for a page key (Page Up and Down,
// Home, End, Space; backward for Page Up and Space with Shift); whether
// it could move.
bool muiScrollPage(muiContext* context, uint32_t container, muiKeyCode code, bool backward,
                   uint64_t timeNs);

// A pointer record's default for scrolling: a touch's or a pen's drag of
// a scroll container pans it, and flings it at the end. Whether it
// panned.
bool muiScrollPointer(muiContext* context, const muiPointerRecord* record);

// Stops the flings of the node at slot and the scroll containers above
// it, as a press there at timeNs does; one past a limit springs back.
void muiScrollStopFlings(muiContext* context, uint32_t slot, uint64_t timeNs);

// The origin of a node's border box on the surface, through its
// ancestors' places and scroll shifts, up to root, or to the top of its
// tree for 0.
void muiScrollOriginOf(const muiContext* context, uint32_t root, uint32_t node, double* xOut,
                       double* yOut);

// Moves the steps easing to where they are at nowNs, after layout.
void muiScrollAdvance(muiContext* context, uint64_t nowNs);

// Whether a step eases a node under root.
bool muiScrollIsEasingUnder(const muiContext* context, uint32_t root);

// Scrolls a node's scrolling ancestors to bring it into view, as
// muiNode_ScrollIntoView does.
void muiScrollReveal(muiContext* context, uint32_t slot);

#endif // MAUL_UI_SRC_SCROLL_H
