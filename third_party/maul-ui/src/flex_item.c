// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Flexbox section 9.2 for one item: the constraint it is sized
// under, its flex base size, its automatic minimum and its contribution
// to a container sized by its content.

#include "flex_item.h"

#include <math.h>

// The child's main size measured from its content under mode; with
// contentOnly, its own limits and ratio left out.
static float ContentMain(const muiFlexFrame* frame, uint32_t child, muiMeasureMode mode,
                         muiMeasureAxis cross, bool contentOnly)
{
    muiSizingInput input = muiFlexChildInput(frame, (muiMeasureAxis){0.0f, mode}, cross);
    input.contentOnly = contentOnly;
    return muiFlexMainOf(frame, frame->solver->solve(frame->solver, child, &input, false));
}

// The base an item with an aspect ratio and a `content` basis takes from
// its cross size (section 9.2.3 B), its content left to its automatic
// minimum: an exact cross size, or in a column the width it fits, as in
// Chrome.
static float RatioBase(const muiFlexFrame* frame, uint32_t child, muiMeasureMode mode,
                       muiMeasureAxis cross)
{
    float ratio = frame->solver->nodes[child - 1].style.sizing.aspectRatio;
    float size = cross.size;
    if (cross.mode != mui_measureExact)
    {
        muiSizingInput input = muiFlexChildInput(frame, (muiMeasureAxis){0.0f, mode}, cross);
        size = muiFlexCrossOf(frame, frame->solver->solve(frame->solver, child, &input, false));
    }
    return frame->row ? size * ratio : size / ratio;
}

// A base from content (section 9.2.3 E): its content's size, its own
// limits left out, asked apart only when it has some, the answer then
// differing; through its aspect ratio from its cross size instead where
// RatioBase says.
static float ContentBase(const muiFlexFrame* frame, uint32_t child, const muiAxisSizing* main,
                         muiMeasureMode mode, muiMeasureAxis cross)
{
    float ratio = frame->solver->nodes[child - 1].style.sizing.aspectRatio;
    if (ratio > 0.0f && !main->definite && (!frame->row || cross.mode == mui_measureExact))
    {
        return RatioBase(frame, child, mode, cross);
    }
    bool limited = !main->minimumAuto || main->maximum < INFINITY;
    return ContentMain(frame, child, mode, cross, limited && ratio <= 0.0f);
}

float muiFlexAutomaticMinimum(const muiFlexFrame* frame, uint32_t child, const muiAxisSizing* main,
                              muiMeasureAxis cross)
{
    // A scroll container's is 0 (section 4.5): it shrinks and scrolls.
    if (frame->solver->nodes[child - 1].style.scrollAxes != mui_scrollNone)
    {
        return 0.0f;
    }
    // A row's ratio item counts its content's own answer, the ratio and
    // its limits applied below (as Chrome).
    float ratio = frame->solver->nodes[child - 1].style.sizing.aspectRatio;
    float content =
        ContentMain(frame, child, mui_measureMinContent, cross, ratio > 0.0f && frame->row);
    if (ratio > 0.0f && cross.mode == mui_measureExact)
    {
        content = fmaxf(content, frame->row ? cross.size * ratio : cross.size / ratio);
    }
    else if (ratio > 0.0f && !frame->row)
    {
        // In a column, the width its content fits through the ratio.
        muiSizingInput input =
            muiFlexChildInput(frame, (muiMeasureAxis){0.0f, mui_measureMaxContent}, cross);
        input.contentOnly = true;
        content =
            fmaxf(content, frame->solver->solve(frame->solver, child, &input, false).width / ratio);
    }
    if (ratio > 0.0f)
    {
        // Floored by its cross minimum through the ratio (section 4.5),
        // padding and border among it, and capped by its cross maximum so
        // too, no lower than that floor: in a row only while its cross
        // size is not definite, as in Chrome.
        const muiFlexItemState* item = &frame->solver->nodes[child - 1].item;
        const muiLayoutStyle* style = &frame->solver->nodes[child - 1].style;
        const muiEdges padding = muiFlexChildPadding(frame, style);
        float floor = fmaxf(item->minCross, muiBoxSum(&padding, style, !frame->row));
        float scale = frame->row ? ratio : 1.0f / ratio;
        bool capped = !frame->row || cross.mode != mui_measureExact;
        float cap = capped ? fmaxf(item->maxCross, floor) * scale : INFINITY;
        content = fminf(fmaxf(content, floor * scale), cap);
    }
    content = fminf(content, main->maximum);
    if (main->definite)
    {
        content = fminf(content, fminf(main->size, main->maximum));
    }
    return content;
}

float muiFlexPrepareItem(const muiFlexFrame* frame, uint32_t child, float* aloneOut)
{
    muiLayoutNode* layout = &frame->solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    muiFlexItemState* item = &layout->item;
    muiAxisSizing main = muiResolveAxis(&style->sizing, frame->row, frame->extentMain);
    muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
    const muiEdges padding = muiFlexChildPadding(frame, style);
    float boxMain = muiBoxSum(&padding, style, frame->row);
    muiMeasureAxis crossConstraint = muiFlexCrossConstraint(frame, style, &cross, true);
    muiEdges margins = muiMarginsOf(style, frame->rtl);
    *item = (muiFlexItemState){
        .marginMain = muiEdgeSum(&margins, frame->row),
        .marginCross = muiEdgeSum(&margins, !frame->row),
        .maxMain = main.maximum,
        .minCross = cross.minimum,
        .maxCross = cross.maximum,
    };
    float base = 0.0f;
    bool fromContent = false;
    bool basisGiven = muiResolveDimension(style->item.basis, frame->extentMain, &base);
    muiMeasureMode mode =
        frame->mainIn.mode == mui_measureMinContent ? mui_measureMinContent : mui_measureMaxContent;
    if (!basisGiven)
    {
        // A definite cross size gives the base through the aspect ratio
        // (section 9.2.3 B) when the child is sized with an exact cross
        // size and an automatic main one. In a column of no definite
        // height a basis that cannot resolve is `content` (section 7.2.3),
        // ignoring the height, as in Chrome; a row resolves it once its
        // width is known and sizes by the width before.
        fromContent =
            !main.definite || (!frame->row && style->item.basis.kind != mui_dimensionAuto);
        base = fromContent ? ContentBase(frame, child, &main, mode, crossConstraint) : main.size;
    }
    item->base = fmaxf(base, boxMain);
    item->innerBase = item->base - boxMain;
    float minimum = main.minimum;
    if (main.minimumAuto && fromContent && style->sizing.aspectRatio <= 0.0f)
    {
        // A base from content is never below its automatic minimum, which
        // therefore only matters if the line shrinks; one through an
        // aspect ratio may be.
        item->minimumPending = true;
        minimum = 0.0f;
    }
    else if (main.minimumAuto)
    {
        minimum = muiFlexAutomaticMinimum(frame, child, &main, crossConstraint);
    }
    item->minMain = fmaxf(minimum, boxMain);
    item->hypothetical = muiClampSize(item->base, item->minMain, item->maxMain, boxMain);
    *aloneOut = item->hypothetical + item->marginMain;
    if (!basisGiven || !frame->row || frame->mainIn.mode == mui_measureExact)
    {
        return *aloneOut;
    }
    float size =
        main.definite ? main.size : ContentMain(frame, child, mode, crossConstraint, false);
    float alone = size;
    if (!(frame->multiLine && mode == mui_measureMinContent))
    {
        alone = style->item.shrink == 0.0f ? fmaxf(size, item->base) : size;
        size = style->item.grow == 0.0f ? fminf(alone, item->base) : alone;
        size = style->item.shrink == 0.0f ? fmaxf(size, item->base) : size;
        if (!main.definite && frame->multiLine)
        {
            // Content alone on a line counts its narrowest, as a wrapping
            // row's max-content size is never under its min-content one.
            float narrowest =
                ContentMain(frame, child, mui_measureMinContent, crossConstraint, false);
            alone = fmaxf(size, narrowest);
        }
    }
    *aloneOut = muiClampSize(alone, item->minMain, item->maxMain, boxMain) + item->marginMain;
    return muiClampSize(size, item->minMain, item->maxMain, boxMain) + item->marginMain;
}
