// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Flexbox's layout algorithm (section 9) for a container's in-flow
// children. A container is run in its own frame of reference: main and
// cross axes, with physical start and end mapped once in Setup. Children
// are sized through the solver, which caches them.

#include "flex.h"

#include "flex_item.h"
#include "flex_resolve.h"
#include "sizing.h"

#include <math.h>

// Whether a child takes part in its line's baseline alignment (section
// 9.4 step 8): it aligns by baseline and neither cross margin is
// automatic. In a column the cross axis is the inline axis, where a box
// has no baseline: one is synthesized from its line-left border edge, so
// the group lines those edges up, as in Chrome.
static bool IsBaselineAligned(const muiFlexFrame* frame, const muiLayoutStyle* child)
{
    return muiFlexAlignOf(frame, child) == mui_alignBaseline &&
           !muiIsMarginAutoStart(child, !frame->row, frame->rtl) &&
           !muiIsMarginAutoEnd(child, !frame->row, frame->rtl);
}

static muiFlexItemState* ItemOf(const muiFlexFrame* frame, uint32_t child)
{
    return &frame->solver->nodes[child - 1].item;
}

static float Gaps(float gap, uint32_t count)
{
    return count > 1 ? gap * (float)(count - 1) : 0.0f;
}

// The in-flow sibling count places after node; 0 past the last.
static uint32_t Skip(const muiFlexFrame* frame, uint32_t node, uint32_t count)
{
    uint32_t at = node;
    for (uint32_t i = 0; i < count && at != 0; i++)
    {
        at = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, at);
    }
    return at;
}

// Computes the automatic minimums PrepareItem left pending, for a line
// that shrinks.
static void ResolvePendingMinimums(const muiFlexFrame* frame, uint32_t first, uint32_t count)
{
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        muiLayoutNode* layout = &frame->solver->nodes[c - 1];
        muiFlexItemState* item = &layout->item;
        if (!item->minimumPending)
        {
            continue;
        }
        const muiLayoutStyle* style = &layout->style;
        muiAxisSizing main = muiResolveAxis(&style->sizing, frame->row, frame->extentMain);
        muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        muiMeasureAxis crossConstraint = muiFlexCrossConstraint(frame, style, &cross, true);
        float minimum = muiFlexAutomaticMinimum(frame, c, &main, crossConstraint);
        const muiEdges padding = muiFlexChildPadding(frame, style);
        item->minMain = fmaxf(minimum, muiBoxSum(&padding, style, frame->row));
        item->minimumPending = false;
        // A content-based base is never below the minimum, so the
        // hypothetical size stands.
    }
}

static muiFlexFrame Setup(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiFlexDirection direction = style->container.direction;
    muiFlexFrame frame = {.solver = solver, .node = node, .style = style};
    frame.row = direction == mui_flexRow || direction == mui_flexRowReverse;
    frame.reverse = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, input->rtl);
    frame.boxMainStart =
        muiEdgeStart(&padding, frame.row) + muiEdgeStart(&style->border, frame.row);
    frame.boxMain = muiBoxSum(&padding, style, frame.row);
    frame.boxCrossStart =
        muiEdgeStart(&padding, !frame.row) + muiEdgeStart(&style->border, !frame.row);
    frame.boxCross = muiBoxSum(&padding, style, !frame.row);
    frame.mainIn = frame.row ? input->width : input->height;
    frame.crossIn = frame.row ? input->height : input->width;
    float parentMain = frame.row ? input->parentWidth : input->parentHeight;
    float parentCross = frame.row ? input->parentHeight : input->parentWidth;
    frame.mainLimits = muiResolveAxis(&style->sizing, frame.row, parentMain);
    frame.crossLimits = muiResolveAxis(&style->sizing, !frame.row, parentCross);
    if (input->contentOnly)
    {
        // The content's answer alone, its own limits left out.
        frame.mainLimits.minimum = frame.crossLimits.minimum = 0.0f;
        frame.mainLimits.maximum = frame.crossLimits.maximum = INFINITY;
    }
    frame.gap = frame.row ? style->container.columnGap : style->container.rowGap;
    frame.crossGap = frame.row ? style->container.rowGap : style->container.columnGap;
    frame.multiLine = style->container.wrap != mui_wrapNone;
    frame.wrapReverse = style->container.wrap == mui_wrapReverse;
    frame.rtl = input->rtl;
    frame.extentMain = -1.0f;
    frame.extentCross = -1.0f;
    bool mainDefinite = !(input->contentHeight && !frame.row);
    bool crossDefinite = !(input->contentHeight && frame.row);
    if (frame.mainIn.mode == mui_measureExact)
    {
        frame.innerMain = fmaxf(frame.mainIn.size - frame.boxMain, 0.0f);
        frame.extentMain = mainDefinite ? frame.innerMain : -1.0f;
    }
    if (frame.crossIn.mode == mui_measureExact)
    {
        frame.innerCross = fmaxf(frame.crossIn.size - frame.boxCross, 0.0f);
        frame.extentCross = crossDefinite ? frame.innerCross : -1.0f;
    }
    return frame;
}

// Section 9.2 and 9.3: every child's hypothetical main size, the
// container's inner main size from its content when not given, then the
// lines and each line's flexible lengths.
static void SizeMain(muiFlexFrame* frame)
{
    const muiTree* tree = frame->solver->tree;
    float sum = 0.0f;
    float widest = 0.0f;
    for (uint32_t c = muiFirstFlowChild(tree, frame->solver->nodes, frame->node); c != 0;
         c = muiNextFlowChild(tree, frame->solver->nodes, c))
    {
        float alone = 0.0f;
        sum += muiFlexPrepareItem(frame, c, &alone);
        widest = fmaxf(widest, alone);
        frame->count++;
    }
    if (frame->mainIn.mode != mui_measureExact)
    {
        // A row that wraps is at its narrowest one child per line, and at
        // its widest at least as wide as each child alone; a column's
        // min-content height is its max-content one, as in CSS.
        bool wraps = frame->row && frame->multiLine;
        bool narrowest = wraps && frame->mainIn.mode == mui_measureMinContent;
        float content = narrowest ? widest : sum + Gaps(frame->gap, frame->count);
        content = wraps ? fmaxf(content, widest) : content;
        float outer = muiClampSize(content + frame->boxMain, frame->mainLimits.minimum,
                                   frame->mainLimits.maximum, frame->boxMain);
        frame->innerMain = outer - frame->boxMain;
    }
    for (uint32_t first = muiFirstFlowChild(tree, frame->solver->nodes, frame->node); first != 0;)
    {
        uint32_t count = muiCollectLine(tree, frame->solver->nodes, first, frame->innerMain,
                                        frame->gap, frame->multiLine);
        float used = Gaps(frame->gap, count);
        for (uint32_t c = first, i = 0; i < count;
             c = muiNextFlowChild(tree, frame->solver->nodes, c), i++)
        {
            used += ItemOf(frame, c)->hypothetical + ItemOf(frame, c)->marginMain;
        }
        if (used > frame->innerMain)
        {
            ResolvePendingMinimums(frame, first, count);
        }
        muiResolveFlexibleLengths(tree, frame->solver->nodes, first, count, frame->innerMain,
                                  Gaps(frame->gap, count));
        ItemOf(frame, first)->lineCount = count;
        frame->lineCount++;
        first = Skip(frame, first, count);
    }
}

// Section 9.4 step 7 and 8: each child's hypothetical cross size at its
// target main size and, aligned by baseline, its ascent there; returns
// the line's cross size, which holds the other children and the
// baseline-aligned ones aligned.
static float HypotheticalCross(const muiFlexFrame* frame, uint32_t first, uint32_t count)
{
    float line = 0.0f;
    float ascent = -INFINITY;
    float descent = -INFINITY;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = ItemOf(frame, c);
        if (frame->ratioHeight && frame->row && !frame->multiLine &&
            muiFlexIsStretched(frame, style))
        {
            // It takes the line's height; only its minimum, padding and
            // border among it, holds the line.
            const muiEdges padding = muiFlexChildPadding(frame, style);
            item->cross = fmaxf(item->minCross, muiBoxSum(&padding, style, !frame->row));
            line = fmaxf(line, item->cross + item->marginCross);
            continue;
        }
        muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        muiMeasureAxis constraint = muiFlexCrossConstraint(frame, style, &cross, false);
        muiSizingInput input = muiFlexChildInput(frame, muiExact(item->target), constraint);
        float size = muiFlexCrossOf(frame, frame->solver->solve(frame->solver, c, &input, false));
        const muiEdges padding = muiFlexChildPadding(frame, style);
        item->cross = muiClampSize(size, item->minCross, item->maxCross,
                                   muiBoxSum(&padding, style, !frame->row));
        float outer = item->cross + item->marginCross;
        if (IsBaselineAligned(frame, style))
        {
            // A column's is synthesized at the border box's line-left edge,
            // its cross end under rtl.
            float own = frame->rtl ? item->cross : 0.0f;
            if (frame->row)
            {
                // Its height its content's, as it is laid out (not
                // stretched, it is so unless given or from its ratio).
                muiSizingInput at =
                    muiFlexChildInput(frame, muiExact(item->target), muiExact(item->cross));
                at.contentHeight =
                    style->sizing.aspectRatio <= 0.0f &&
                    !muiResolveAxis(&style->sizing, false, frame->extentCross).definite;
                own = frame->solver->baseline(frame->solver, c, &at);
            }
            muiEdges margins = muiMarginsOf(style, frame->rtl);
            item->ascent = muiEdgeStart(&margins, !frame->row) + own;
            ascent = fmaxf(ascent, item->ascent);
            descent = fmaxf(descent, outer - item->ascent);
            ItemOf(frame, first)->lineBaselines = true;
        }
        else
        {
            line = fmaxf(line, outer);
        }
    }
    return ItemOf(frame, first)->lineBaselines ? fmaxf(line, ascent + descent) : line;
}

// Where a line's baseline-aligned children put its baseline, from its
// cross start: they are a group flush with the line's cross start, which
// wrap-reverse puts at the physical end.
static float LineBaseline(const muiFlexFrame* frame, uint32_t first, uint32_t count)
{
    float ascent = -INFINITY;
    float descent = -INFINITY;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        if (IsBaselineAligned(frame, &frame->solver->nodes[c - 1].style))
        {
            const muiFlexItemState* item = ItemOf(frame, c);
            ascent = fmaxf(ascent, item->ascent);
            descent = fmaxf(descent, item->cross + item->marginCross - item->ascent);
        }
    }
    float line = ItemOf(frame, first)->lineCross;
    return frame->wrapReverse ? line - descent : ascent;
}

// Section 9.4 step 11: stretched children take their line's cross size.
static void Stretch(muiFlexFrame* frame, uint32_t first, uint32_t count)
{
    float line = ItemOf(frame, first)->lineCross;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = ItemOf(frame, c);
        muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        if (!cross.definite && muiFlexIsStretched(frame, style))
        {
            const muiEdges padding = muiFlexChildPadding(frame, style);
            float measured = item->cross;
            item->cross = muiClampSize(line - item->marginCross, item->minCross, item->maxCross,
                                       muiBoxSum(&padding, style, !frame->row));
            frame->widened = frame->widened || item->cross > measured;
        }
    }
}

// Section 9.6 step 15: align-content places the lines in the free cross
// space and stretches them into it.
static void DistributeLines(const muiFlexFrame* frame, float used)
{
    float lead = 0.0f;
    float between = 0.0f;
    float grow = 0.0f;
    muiAlignContentSpacing(frame->style->container.alignContent, frame->innerCross - used,
                           frame->lineCount, frame->wrapReverse, &lead, &between, &grow);
    float offset = lead;
    for (uint32_t first = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         first != 0; first = Skip(frame, first, ItemOf(frame, first)->lineCount))
    {
        muiFlexItemState* head = ItemOf(frame, first);
        head->lineCross += grow;
        head->lineOffset = offset;
        offset += head->lineCross + frame->crossGap + between;
    }
}

// Section 9.4: the lines' cross sizes, the container's, the lines'
// places, and stretched children's cross sizes.
// Section 9.9.2: a single-line column sized by its content is as wide as
// its widest item's contribution, its width with its height left open, as
// Chrome sizes it, not as wide as its items are at their flexed heights.
static float ColumnContribution(const muiFlexFrame* frame)
{
    float widest = 0.0f;
    for (uint32_t c = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         c != 0; c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c))
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        const muiFlexItemState* item = ItemOf(frame, c);
        muiAxisSizing cross = muiResolveAxis(&style->sizing, true, frame->extentCross);
        muiMeasureAxis constraint = muiFlexCrossConstraint(frame, style, &cross, false);
        // A height the item's style gives is its own, open otherwise.
        muiAxisSizing main = muiResolveAxis(&style->sizing, false, frame->extentMain);
        const muiEdges padding = muiFlexChildPadding(frame, style);
        muiMeasureAxis height = main.definite
                                    ? muiExact(muiClampSize(main.size, main.minimum, main.maximum,
                                                            muiBoxSum(&padding, style, false)))
                                    : (muiMeasureAxis){0.0f, mui_measureMaxContent};
        muiSizingInput input = muiFlexChildInput(frame, height, constraint);
        float size = frame->solver->solve(frame->solver, c, &input, false).width;
        size = muiClampSize(size, item->minCross, item->maxCross, muiBoxSum(&padding, style, true));
        widest = fmaxf(widest, size + item->marginCross);
    }
    return widest;
}

static void SizeCross(muiFlexFrame* frame)
{
    float total = 0.0f;
    for (uint32_t first = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         first != 0; first = Skip(frame, first, ItemOf(frame, first)->lineCount))
    {
        ItemOf(frame, first)->lineCross =
            HypotheticalCross(frame, first, ItemOf(frame, first)->lineCount);
        total += ItemOf(frame, first)->lineCross;
    }
    total += Gaps(frame->crossGap, frame->lineCount);
    frame->contentCross = total + frame->boxCross;
    if (!frame->row && !frame->multiLine && frame->crossIn.mode != mui_measureExact)
    {
        total = ColumnContribution(frame);
    }
    if (frame->crossIn.mode != mui_measureExact)
    {
        float outer = muiClampSize(total + frame->boxCross, frame->crossLimits.minimum,
                                   frame->crossLimits.maximum, frame->boxCross);
        frame->innerCross = outer - frame->boxCross;
    }
    uint32_t first = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
    if (!frame->multiLine && first != 0)
    {
        // A single line is as large as the container's inner cross size.
        ItemOf(frame, first)->lineCross = frame->innerCross;
    }
    else if (first != 0)
    {
        DistributeLines(frame, total);
    }
    for (uint32_t line = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         line != 0; line = Skip(frame, line, ItemOf(frame, line)->lineCount))
    {
        Stretch(frame, line, ItemOf(frame, line)->lineCount);
    }
}

// Section 9.6 step 13: where automatic cross margins put a child in its
// line, as its physical offset from the line's start; overflow sets the
// start margin to zero.
static float AutoCrossOffset(const muiFlexFrame* frame, const muiLayoutStyle* style,
                             const muiFlexItemState* item, float line)
{
    muiEdges margins = muiMarginsOf(style, frame->rtl);
    float start = muiEdgeStart(&margins, !frame->row);
    float freeSpace = line - item->cross - item->marginCross;
    bool autoStart = muiIsMarginAutoStart(style, !frame->row, frame->rtl);
    bool autoEnd = muiIsMarginAutoEnd(style, !frame->row, frame->rtl);
    if (freeSpace <= 0.0f)
    {
        // An automatic start margin is already zero in start.
        return start;
    }
    if (autoStart)
    {
        return start + (autoEnd ? freeSpace / 2.0f : freeSpace);
    }
    return start;
}

// The child's physical offset from its line's start on the cross axis,
// its margin included. Under wrap-reverse the cross start is the
// physical end, so start and end alignment swap.
static float CrossOffset(const muiFlexFrame* frame, const muiLayoutStyle* style,
                         const muiFlexItemState* item, float line, float baseline)
{
    if (muiIsMarginAutoStart(style, !frame->row, frame->rtl) ||
        muiIsMarginAutoEnd(style, !frame->row, frame->rtl))
    {
        return AutoCrossOffset(frame, style, item, line);
    }
    muiEdges margins = muiMarginsOf(style, frame->rtl);
    float start = muiEdgeStart(&margins, !frame->row);
    float end = muiEdgeEnd(&margins, !frame->row);
    muiAlign align = muiFlexAlignOf(frame, style);
    if (align == mui_alignBaseline)
    {
        // Section 9.6 step 14: the line's baseline-aligned children share
        // its baseline.
        return baseline - item->ascent + start;
    }
    if (frame->wrapReverse && (align == mui_alignStart || align == mui_alignEnd))
    {
        align = align == mui_alignStart ? mui_alignEnd : mui_alignStart;
    }
    else if (frame->wrapReverse && align == mui_alignStretch)
    {
        align = mui_alignEnd;
    }
    switch (align)
    {
    case mui_alignEnd:
        return line - item->cross - end;
    case mui_alignCenter:
        return start + (line - item->marginCross - item->cross) / 2.0f;
    default:
        return start;
    }
}

// What a child is laid out in full under: its final sizes, against the
// container's content box.
static muiSizingInput FinalInput(const muiFlexFrame* frame, uint32_t child)
{
    const muiFlexItemState* item = ItemOf(frame, child);
    const muiLayoutStyle* style = &frame->solver->nodes[child - 1].style;
    muiSizingInput input = muiFlexChildInput(frame, muiExact(item->target), muiExact(item->cross));
    float extentMain = frame->extentMain >= 0.0f ? frame->innerMain : -1.0f;
    float extentCross = frame->extentCross >= 0.0f ? frame->innerCross : -1.0f;
    input.parentWidth = frame->row ? frame->innerMain : extentCross;
    input.parentHeight = frame->row ? extentCross : extentMain;
    // Its height is definite when stretched (once its line's size is,
    // section 9.8), given, or in a row from its aspect ratio; in a column,
    // after flexing, when the column's height or its own basis is
    // definite, or its ratio gives it from the width it fits, its height
    // given counting only under an automatic basis, which a given one
    // replaces (both as in Chrome). Otherwise it is its content's.
    float base = 0.0f;
    if (frame->row)
    {
        input.contentHeight = !muiFlexIsStretched(frame, style) &&
                              style->sizing.aspectRatio <= 0.0f &&
                              !muiResolveAxis(&style->sizing, false, frame->extentCross).definite;
    }
    else
    {
        input.contentHeight = frame->extentMain < 0.0f && style->sizing.aspectRatio <= 0.0f &&
                              !muiResolveDimension(style->item.basis, frame->extentMain, &base) &&
                              (style->item.basis.kind != mui_dimensionAuto ||
                               !muiResolveAxis(&style->sizing, false, frame->extentMain).definite);
    }
    return input;
}

// Sets a child's rectangle from its main and cross offsets within the
// content box and lays it out in full.
static void PlaceChild(const muiFlexFrame* frame, uint32_t child, float main, float cross)
{
    muiLayoutNode* layout = &frame->solver->nodes[child - 1];
    const muiFlexItemState* item = &layout->item;
    float mainAt = frame->boxMainStart + main;
    float crossAt = frame->boxCrossStart + cross;
    layout->rect = frame->row ? (muiRect){mainAt, crossAt, item->target, item->cross}
                              : (muiRect){crossAt, mainAt, item->cross, item->target};
    muiSizingInput input = FinalInput(frame, child);
    (void)frame->solver->solve(frame->solver, child, &input, true);
}

// The line's physical start on the cross axis.
static float LineStart(const muiFlexFrame* frame, const muiFlexItemState* head)
{
    return frame->wrapReverse ? frame->innerCross - head->lineOffset - head->lineCross
                              : head->lineOffset;
}

// A child's offsets from the container's content box.
typedef struct Offsets
{
    float main;
    float cross;
} Offsets;

// Section 9.5 and 9.6: justifies one line and places its children; with
// a probe, places none and returns where the probe goes.
static Offsets PlaceLine(const muiFlexFrame* frame, uint32_t first, uint32_t count, uint32_t probe)
{
    const muiTree* tree = frame->solver->tree;
    const muiFlexItemState* head = ItemOf(frame, first);
    float used = Gaps(frame->gap, count);
    uint32_t autoMargins = 0;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        used += ItemOf(frame, c)->target + ItemOf(frame, c)->marginMain;
        autoMargins += (uint32_t)muiIsMarginAutoStart(style, frame->row, frame->rtl) +
                       (uint32_t)muiIsMarginAutoEnd(style, frame->row, frame->rtl);
    }
    float freeSpace = frame->innerMain - used;
    float lead = 0.0f;
    float between = 0.0f;
    float autoShare = 0.0f;
    // Section 9.5 step 12: automatic margins take positive free space
    // before justification sees it.
    if (autoMargins > 0 && freeSpace > 0.0f)
    {
        autoShare = freeSpace / (float)autoMargins;
    }
    else
    {
        muiJustifySpacing(frame->style->container.justify, freeSpace, count, frame->reverse, &lead,
                          &between);
    }
    float lineStart = LineStart(frame, head);
    float baseline = head->lineBaselines ? LineBaseline(frame, first, count) : NAN;
    float flow = lead;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        const muiFlexItemState* item = ItemOf(frame, c);
        muiEdges margins = muiMarginsOf(style, frame->rtl);
        bool autoStart = muiIsMarginAutoStart(style, frame->row, frame->rtl);
        bool autoEnd = muiIsMarginAutoEnd(style, frame->row, frame->rtl);
        float start = muiEdgeStart(&margins, frame->row) + (autoStart ? autoShare : 0.0f);
        float end = muiEdgeEnd(&margins, frame->row) + (autoEnd ? autoShare : 0.0f);
        // In a reversed container the flow starts at the physical end.
        flow += frame->reverse ? end : start;
        float main = frame->reverse ? frame->innerMain - flow - item->target : flow;
        flow += item->target + (frame->reverse ? start : end) + frame->gap + between;
        float cross = lineStart + CrossOffset(frame, style, item, head->lineCross, baseline);
        if (c == probe)
        {
            return (Offsets){main, cross};
        }
        if (probe == 0)
        {
            PlaceChild(frame, c, main, cross);
        }
    }
    return (Offsets){0.0f, 0.0f};
}

// Places every line's children; with a line and a probe in it, places
// none and returns where the probe goes.
static Offsets Place(const muiFlexFrame* frame, uint32_t line, uint32_t probe)
{
    Offsets at = {0.0f, 0.0f};
    for (uint32_t first = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         first != 0; first = Skip(frame, first, ItemOf(frame, first)->lineCount))
    {
        if (line == 0 || first == line)
        {
            at = PlaceLine(frame, first, ItemOf(frame, first)->lineCount, probe);
        }
    }
    return at;
}

// Section 8.5 as Chrome reads it: the first baseline comes from the line
// at the top of a row (the last under wrap-reverse) or the inline start
// of a column, from its baseline-aligned children if it has any, else
// from its child first in the container's direction, the last when it is
// reversed.
static float FirstBaseline(const muiFlexFrame* frame)
{
    const muiSolver* solver = frame->solver;
    uint32_t line = 0;
    for (uint32_t first = muiFirstFlowChild(solver->tree, solver->nodes, frame->node); first != 0;
         first = Skip(frame, first, ItemOf(frame, first)->lineCount))
    {
        line = line == 0 || frame->wrapReverse ? first : line;
    }
    if (line == 0)
    {
        return NAN;
    }
    const muiFlexItemState* head = ItemOf(frame, line);
    uint32_t child = line;
    for (uint32_t c = line, i = 0; i < head->lineCount;
         c = muiNextFlowChild(solver->tree, solver->nodes, c), i++)
    {
        child = frame->reverse ? c : child;
    }
    if (head->lineBaselines && frame->row)
    {
        return frame->boxCrossStart + LineStart(frame, head) +
               LineBaseline(frame, line, head->lineCount);
    }
    muiSizingInput at = FinalInput(frame, child);
    float own = solver->baseline(solver, child, &at);
    Offsets offsets = Place(frame, line, child);
    return own +
           (frame->row ? frame->boxCrossStart + offsets.cross : frame->boxMainStart + offsets.main);
}

// Whether a column sized at an open width lays out otherwise at the width
// it found: a child was stretched wider than it was measured, or sizes by
// the container's width.
static bool WidthMatters(const muiFlexFrame* frame)
{
    for (uint32_t c = muiFirstFlowChild(frame->solver->tree, frame->solver->nodes, frame->node);
         c != 0 && !frame->widened;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c))
    {
        const muiSizing* sizing = &frame->solver->nodes[c - 1].style.sizing;
        if (muiIsScaled(sizing->width) || muiIsScaled(sizing->minWidth) ||
            muiIsScaled(sizing->maxWidth))
        {
            return true;
        }
    }
    return frame->widened;
}

// Sizes a row container's width as fit-content: its max-content size
// when that fits the space, else the space but no less than its
// min-content size.
static muiMeasureAxis FitMain(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    muiSizingInput probe = *input;
    float space = probe.width.size;
    probe.width = (muiMeasureAxis){0.0f, mui_measureMaxContent};
    float size = solver->solve(solver, node, &probe, false).width;
    if (size > space)
    {
        probe.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
        size = fmaxf(solver->solve(solver, node, &probe, false).width, space);
    }
    return muiExact(size);
}

// Sizes a container and, with perform, places its children; with
// baseline, finds its first baseline instead. The one caller of the
// steps, so that they inline into it.
static muiSize Flex(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                    bool perform, float* baseline, bool ratioHeight)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    bool row = style->container.direction == mui_flexRow ||
               style->container.direction == mui_flexRowReverse;
    muiSizingInput fitted = *input;
    muiMeasureAxis* main = row ? &fitted.width : &fitted.height;
    if (main->mode == mui_measureAtMost)
    {
        // Fit-content on the vertical axis is the max-content size, as
        // min-content and max-content heights are the same in CSS.
        *main = row ? FitMain(solver, node, input) : (muiMeasureAxis){0.0f, mui_measureMaxContent};
    }
    muiFlexFrame frame;
    for (;;)
    {
        frame = Setup(solver, node, &fitted);
        frame.ratioHeight = ratioHeight;
        SizeMain(&frame);
        SizeCross(&frame);
        if (row || fitted.width.mode != mui_measureAtMost || !WidthMatters(&frame))
        {
            break;
        }
        // A column's width comes first, as CSS sizes a box's width before
        // its height: its items are laid out again at the width it found.
        fitted.width = muiExact(frame.innerCross + frame.boxCross);
    }
    if (baseline != nullptr)
    {
        *baseline = FirstBaseline(&frame);
    }
    else if (perform)
    {
        (void)Place(&frame, 0, 0);
    }
    float mainSize = frame.innerMain + frame.boxMain;
    float crossSize = ratioHeight ? frame.contentCross : frame.innerCross + frame.boxCross;
    return row ? (muiSize){mainSize, crossSize} : (muiSize){crossSize, mainSize};
}

muiSize muiLayoutFlex(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                      bool perform)
{
    return Flex(solver, node, input, perform, nullptr, false);
}

float muiFlexBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    float baseline = NAN;
    (void)Flex(solver, node, input, false, &baseline, false);
    return baseline;
}

float muiFlexRatioContentHeight(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    return Flex(solver, node, input, false, nullptr, true).height;
}
