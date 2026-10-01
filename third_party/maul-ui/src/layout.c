// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public layout functions: authored values in, rectangles out, and
// the run of the solver over the part of a tree that changed.

#include "maul-ui/layout.h"

#include "animation.h"
#include "context.h"
#include "layout_node.h"
#include "property.h"
#include "restyle.h"
#include "solve.h"
#include "tree.h"

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

// Forgets what the solver remembers about every node on a path to a
// change, then clears their layout flags.
static void Invalidate(muiContext* context, uint32_t root)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageLayout); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageLayout))
    {
        context->layout[at - 1].cache = (muiLayoutCache){0};
    }
    muiTreeSweep(tree, root, mui_stageLayout);
}

muiResult muiComputeLayout(muiContext* context, muiNodeId rootId, const muiLayoutInput* input)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (input == nullptr || !IsLength(input->availableWidth) || !IsLength(input->availableHeight))
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
    const muiMotion motion = {&context->animations, context->layout, context->visual,
                              context->style.nodes, &context->tree,  context->text,
                              context->textRecords};
    bool finish = context->environment.reducedMotion;
    muiAdvanceAnimations(&motion, now, finish);
    muiRestyle(context, root, now);
    muiAdvanceAnimations(&motion, now, finish);
    Invalidate(context, root);
    muiSolver solver = {
        .tree = &context->tree,
        .nodes = context->layout,
        .measure = input->measure,
        .measureUser = input->measureUser,
        .solve = muiSolveNode,
        .restyle = &context->tree,
        .painted = context->draw.states,
    };
    muiSizingInput sizingInput = muiRootInput(&context->layout[root - 1].style,
                                              input->availableWidth, input->availableHeight);
    context->inHostCall = true;
    muiSize size = muiSolveNode(&solver, root, &sizingInput, false);
    sizingInput.width = (muiMeasureAxis){size.width, mui_measureExact};
    sizingInput.height = (muiMeasureAxis){size.height, mui_measureExact};
    (void)muiSolveNode(&solver, root, &sizingInput, true);
    context->inHostCall = false;
    const muiRect rect = {0.0f, 0.0f, size.width, size.height};
    context->layout[root - 1].rect = rect;
    if (!muiIsSameRect(rect, context->draw.states[root - 1].rect))
    {
        muiTreeMark(&context->tree, root, mui_stagePaint);
    }
    return mui_success;
}

muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    return slot != 0 ? context->layout[slot - 1].rect : (muiRect){0.0f, 0.0f, 0.0f, 0.0f};
}

bool muiIsUpdatePending(const muiContext* context, muiNodeId rootId)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, rootId) : 0;
    return slot != 0 && ((muiTreeAt(&context->tree, slot)->dirty.subtree &
                          (mui_stageStyle | mui_stageLayout)) != 0 ||
                         muiIsAnimatingUnder(&context->animations, &context->tree, slot));
}
