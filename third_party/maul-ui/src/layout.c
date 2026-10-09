// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public layout functions: authored values in, rectangles out, and
// the run of the solver over the part of a tree that changed.

#include "maul-ui/layout.h"

#include "animation.h"
#include "context.h"
#include "exit.h"
#include "layout_bound.h"
#include "layout_node.h"
#include "popup.h"
#include "property.h"
#include "restyle.h"
#include "scroll.h"
#include "solve.h"
#include "tree.h"
#include "virtual.h"

#include "maul-ui/style.h"

#include <math.h>

muiLayoutStyle muiDefaultLayoutStyle(void)
{
    return *muiLayoutDefaults();
}

static bool IsLength(float value)
{
    return isfinite(value) && value >= 0.0f;
}

muiResult muiNode_SetLayoutStyle(muiContext* context, muiNodeId nodeId, const muiLayoutStyle* style)
{
    return muiNode_SetLayoutValues(context, nodeId, style, MUI_LAYOUT_PROPERTIES);
}

muiResult muiNode_GetLayoutStyle(const muiContext* context, muiNodeId nodeId,
                                 muiLayoutStyle* styleOut)
{
    if (context == nullptr || styleOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *styleOut = context->layout[slot - 1].style;
    return mui_success;
}

bool muiNode_IsRightToLeft(const muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr || nodeId.index1 == 0)
    {
        return false;
    }
    for (uint32_t at = muiTreeResolve(&context->tree, nodeId); at != 0;
         at = muiTreeAt(&context->tree, at)->links.parent)
    {
        muiTextDirection direction = context->layout[at - 1].style.textDirection;
        if (direction != mui_textInherit)
        {
            return direction == mui_textRightToLeft;
        }
    }
    return false;
}

muiResult muiNode_MarkContentChanged(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0)
    {
        muiTreeMarkLayout(&context->tree, slot);
    }
    return status;
}

// Whether a safe area is one: finite insets of 0 or more.
static bool IsSafeArea(const muiSides* safe)
{
    return IsLength(safe->top) && IsLength(safe->right) && IsLength(safe->bottom) &&
           IsLength(safe->left);
}

// Lays out again the nodes below a root that pad by the safe area, when
// it is not the one last given.
static void NoteSafeArea(muiContext* context, uint32_t root, const muiSides* safe)
{
    const muiSides* last = &context->safeArea;
    if (safe->top == last->top && safe->right == last->right && safe->bottom == last->bottom &&
        safe->left == last->left)
    {
        return;
    }
    context->safeArea = *safe;
    for (uint32_t at = root; at != 0; at = muiTreeNextIn(&context->tree, root, at))
    {
        if (context->layout[at - 1].style.safeArea != 0)
        {
            muiTreeMarkLayout(&context->tree, at);
        }
    }
}

// Forgets what the solver remembers about every node on a path to a
// change.
static void Invalidate(muiContext* context, uint32_t root)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageLayout); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageLayout))
    {
        context->layout[at - 1].cache = (muiLayoutCache){0};
    }
}

// Sizes the root in the host's space and lays its subtree out: a change
// its subtree's answers bound is laid out there; any other clears every
// owing node's cache.
static muiSize SolveRoot(muiContext* context, const muiSolver* solver, uint32_t root,
                         const muiLayoutInput* input)
{
    muiSizingInput sizingInput = muiRootInput(&context->layout[root - 1].style, &input->safeArea,
                                              input->availableWidth, input->availableHeight);
    context->inHostCall = true;
    if (!muiBoundLayout(solver, root))
    {
        Invalidate(context, root);
    }
    muiTreeSweep(&context->tree, root, mui_stageLayout);
    muiRatioRootWidth(solver, root, &sizingInput);
    muiSize size = muiSolveNode(solver, root, &sizingInput, false);
    // A height not given is the root's content's, as an automatic one,
    // unless its aspect ratio gives it.
    sizingInput.contentHeight = sizingInput.height.mode != mui_measureExact &&
                                context->layout[root - 1].style.sizing.aspectRatio <= 0.0f;
    sizingInput.width = (muiMeasureAxis){size.width, mui_measureExact};
    sizingInput.height = (muiMeasureAxis){size.height, mui_measureExact};
    (void)muiSolveNode(solver, root, &sizingInput, true);
    context->inHostCall = false;
    return size;
}

muiResult muiComputeLayout(muiContext* context, muiNodeId rootId, const muiLayoutInput* input)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (input == nullptr || !IsLength(input->availableWidth) || !IsLength(input->availableHeight) ||
        !IsSafeArea(&input->safeArea))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t root = muiResolveEdit(context, rootId, &status);
    if (root == 0)
    {
        return status;
    }
    if (muiTreeAt(&context->tree, root)->links.parent != 0)
    {
        return muiRefuse(context);
    }
    // Transitions move to now first, so that changes start from where
    // values are; once more after styling, so that one with no length ends
    // in this run.
    uint64_t now = input->timeNs > context->lastTimeNs ? input->timeNs : context->lastTimeNs;
    context->lastTimeNs = now;
    const muiMotion motion = {&context->animations, context->layout,     context->visual,
                              context->style.nodes, &context->tree,      context->text,
                              context->textRecords, context->interaction};
    bool finish = context->environment.reducedMotion;
    muiAdvanceAnimations(&motion, now, finish);
    muiRestyle(context, root, now);
    muiAdvanceAnimations(&motion, now, finish);
    // Exits whose transitions have ended, or never began, are reported.
    muiExitAdvance(context, root);
    NoteSafeArea(context, root, &input->safeArea);
    muiSolver solver = {
        .tree = &context->tree,
        .nodes = context->layout,
        .measure = input->measure,
        .measureUser = input->measureUser,
        .measureBaseline = input->baseline,
        .solve = muiSolveNode,
        .baseline = muiSolveBaseline,
        .restyle = &context->tree,
        .painted = context->draw.states,
        .scrolls = context->scrolls,
        .work = &context->work,
        .paddings = context->paddings,
        .safeArea = input->safeArea,
    };
    muiSize size = SolveRoot(context, &solver, root, input);
    const muiRect rect = {0.0f, 0.0f, size.width, size.height};
    context->layout[root - 1].rect = rect;
    if (!muiIsSameRect(rect, context->draw.states[root - 1].rect))
    {
        muiTreeMark(&context->tree, root, mui_stagePaint);
    }
    // Virtual lists measure and place their items and size their content;
    // steps easing move to now, within the limits that leaves; the lists
    // find their windows where scrolling left them.
    muiVirtualPlace(context, root);
    muiScrollAdvance(context, input->timeNs);
    muiVirtualWindows(context, root);
    // Popups go beside their anchors where layout and scrolling left them.
    muiPlacePopups(context, root);
    return mui_success;
}

muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->layout[slot - 1].rect : (muiRect){0.0f, 0.0f, 0.0f, 0.0f};
}

muiRect muiNode_GetContentRect(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? muiContentBoxOf(&context->layout[slot - 1], &context->paddings[slot - 1])
                     : (muiRect){0.0f, 0.0f, 0.0f, 0.0f};
}

bool muiIsUpdatePending(const muiContext* context, muiNodeId rootId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, rootId) : 0;
    return slot != 0 && ((muiTreeAt(&context->tree, slot)->dirty.subtree &
                          (mui_stageStyle | mui_stageLayout)) != 0 ||
                         muiIsAnimatingUnder(&context->animations, &context->tree, slot) ||
                         muiScrollIsEasingUnder(context, slot));
}
