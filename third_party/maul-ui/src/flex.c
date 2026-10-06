// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Flexbox's layout algorithm (section 9) for a container's in-flow
// children. A container is run in its own frame of reference: main and
// cross axes, with physical start and end mapped once in Setup. Children
// are sized through the solver, which caches them.

#include "flex.h"

#include "flex_resolve.h"
#include "sizing.h"

#include <math.h>

// A container in its own frame: main and cross axes, its padding and
// border per side, its constraints and limits, and what it has resolved
// so far.
typedef struct Frame
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
    // The children's percentage bases along each axis, negative when
    // indefinite.
    float extentMain;
    float extentCross;
    float gap;
    float crossGap;
    bool multiLine;
    bool wrapReverse;
    // The container's direction, which its children inherit.
    bool rtl;
    uint32_t count;
    uint32_t lineCount;
    float innerMain;
    float innerCross;
} Frame;

static muiSizingInput ChildInput(const Frame* frame, muiMeasureAxis main, muiMeasureAxis cross)
{
    muiSizingInput input;
    input.width = frame->row ? main : cross;
    input.height = frame->row ? cross : main;
    input.parentWidth = frame->row ? frame->extentMain : frame->extentCross;
    input.parentHeight = frame->row ? frame->extentCross : frame->extentMain;
    input.rtl = frame->rtl;
    return input;
}

static float MainOf(const Frame* frame, muiSize size)
{
    return frame->row ? size.width : size.height;
}

static float CrossOf(const Frame* frame, muiSize size)
{
    return frame->row ? size.height : size.width;
}

static muiAlign AlignOf(const Frame* frame, const muiLayoutStyle* child)
{
    return child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                  : frame->style->container.alignItems;
}

// Whether a child takes part in its line's baseline alignment (section
// 9.4 step 8): it aligns by baseline in a row, whose main axis is the
// inline axis, and neither cross margin is automatic.
static bool IsBaselineAligned(const Frame* frame, const muiLayoutStyle* child)
{
    return frame->row && AlignOf(frame, child) == mui_alignBaseline &&
           !muiIsMarginAutoStart(child, false) && !muiIsMarginAutoEnd(child, false);
}

// Whether a child takes its line's cross size: it aligns by stretch and
// neither cross margin is automatic.
static bool IsStretched(const Frame* frame, const muiLayoutStyle* child)
{
    return AlignOf(frame, child) == mui_alignStretch && !muiIsMarginAutoStart(child, !frame->row) &&
           !muiIsMarginAutoEnd(child, !frame->row);
}

// The constraint a child is sized under on the cross axis: its own
// definite size; with stretch, the line's size when the container's cross
// size is definite; otherwise fit-content within the container.
static muiMeasureAxis CrossConstraint(const Frame* frame, const muiLayoutStyle* child,
                                      const muiAxisSizing* cross, bool stretch)
{
    float boxCross = muiBoxSum(child, !frame->row);
    muiEdges margins = muiMarginsOf(child);
    float margin = muiEdgeSum(&margins, !frame->row);
    if (cross->definite)
    {
        return muiExact(muiClampSize(cross->size, cross->minimum, cross->maximum, boxCross));
    }
    if (frame->crossIn.mode == mui_measureExact)
    {
        float space = fmaxf(frame->innerCross - margin, 0.0f);
        // Only a single line's size is known before the lines are.
        if (stretch && !frame->multiLine && IsStretched(frame, child))
        {
            return muiExact(muiClampSize(space, cross->minimum, cross->maximum, boxCross));
        }
        return (muiMeasureAxis){space, mui_measureAtMost};
    }
    if (frame->crossIn.mode == mui_measureAtMost)
    {
        float space = frame->crossIn.size - frame->boxCross - margin;
        return (muiMeasureAxis){fmaxf(space, 0.0f), mui_measureAtMost};
    }
    return (muiMeasureAxis){0.0f, frame->crossIn.mode};
}

// The child's main size measured from its content under mode.
static float ContentMain(const Frame* frame, uint32_t child, muiMeasureMode mode,
                         muiMeasureAxis cross)
{
    muiSizingInput input = ChildInput(frame, (muiMeasureAxis){0.0f, mode}, cross);
    return MainOf(frame, frame->solver->solve(frame->solver, child, &input, false));
}

// CSS Flexbox section 4.5: for any item but a scroll container, the
// smaller of the specified size and the min-content size, each within
// the maximum. A size the aspect ratio gives is not a specified size: the
// content wins over it, as CSS Sizing 4 says for the ratio-dependent
// axis.
static float AutomaticMinimum(const Frame* frame, uint32_t child, const muiAxisSizing* main,
                              muiMeasureAxis cross)
{
    // A scroll container's is 0 (section 4.5): it shrinks and scrolls.
    if (frame->solver->nodes[child - 1].style.scrollAxes != mui_scrollNone)
    {
        return 0.0f;
    }
    float content = fminf(ContentMain(frame, child, mui_measureMinContent, cross), main->maximum);
    if (main->definite)
    {
        content = fminf(content, fminf(main->size, main->maximum));
    }
    return content;
}

// Section 9.2: a child's margins, limits, flex base size and
// hypothetical main size.
static void PrepareItem(const Frame* frame, uint32_t child)
{
    muiLayoutNode* layout = &frame->solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    muiFlexItemState* item = &layout->item;
    muiAxisSizing main = muiResolveAxis(&style->sizing, frame->row, frame->extentMain);
    muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
    float boxMain = muiBoxSum(style, frame->row);
    muiMeasureAxis crossConstraint = CrossConstraint(frame, style, &cross, true);
    muiEdges margins = muiMarginsOf(style);
    *item = (muiFlexItemState){
        .marginMain = muiEdgeSum(&margins, frame->row),
        .marginCross = muiEdgeSum(&margins, !frame->row),
        .maxMain = main.maximum,
        .minCross = cross.minimum,
        .maxCross = cross.maximum,
    };
    float base = 0.0f;
    bool fromContent = false;
    if (!muiResolveDimension(style->item.basis, frame->extentMain, &base))
    {
        muiMeasureMode mode = frame->mainIn.mode == mui_measureMinContent ? mui_measureMinContent
                                                                          : mui_measureMaxContent;
        // A definite cross size gives the base through the aspect ratio
        // (section 9.2.3 B) when the child is sized with an exact cross
        // size and an automatic main one.
        fromContent = !main.definite;
        base = main.definite ? main.size : ContentMain(frame, child, mode, crossConstraint);
    }
    item->base = fmaxf(base, boxMain);
    item->innerBase = item->base - boxMain;
    float minimum = main.minimum;
    if (main.minimumAuto && fromContent)
    {
        // A base from content is never below its automatic minimum, which
        // therefore only matters if the line shrinks.
        item->minimumPending = true;
        minimum = 0.0f;
    }
    else if (main.minimumAuto)
    {
        minimum = AutomaticMinimum(frame, child, &main, crossConstraint);
    }
    item->minMain = fmaxf(minimum, boxMain);
    item->hypothetical = muiClampSize(item->base, item->minMain, item->maxMain, boxMain);
}

static muiFlexItemState* ItemOf(const Frame* frame, uint32_t child)
{
    return &frame->solver->nodes[child - 1].item;
}

static float Gaps(float gap, uint32_t count)
{
    return count > 1 ? gap * (float)(count - 1) : 0.0f;
}

// The in-flow sibling count places after node; 0 past the last.
static uint32_t Skip(const Frame* frame, uint32_t node, uint32_t count)
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
static void ResolvePendingMinimums(const Frame* frame, uint32_t first, uint32_t count)
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
        muiMeasureAxis crossConstraint = CrossConstraint(frame, style, &cross, true);
        float minimum = AutomaticMinimum(frame, c, &main, crossConstraint);
        item->minMain = fmaxf(minimum, muiBoxSum(style, frame->row));
        item->minimumPending = false;
        // A content-based base is never below the minimum, so the
        // hypothetical size stands.
    }
}

static Frame Setup(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiFlexDirection direction = style->container.direction;
    Frame frame = {.solver = solver, .node = node, .style = style};
    frame.row = direction == mui_flexRow || direction == mui_flexRowReverse;
    frame.reverse = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
    frame.boxMainStart =
        muiEdgeStart(&style->padding, frame.row) + muiEdgeStart(&style->border, frame.row);
    frame.boxMain = muiBoxSum(style, frame.row);
    frame.boxCrossStart =
        muiEdgeStart(&style->padding, !frame.row) + muiEdgeStart(&style->border, !frame.row);
    frame.boxCross = muiBoxSum(style, !frame.row);
    frame.mainIn = frame.row ? input->width : input->height;
    frame.crossIn = frame.row ? input->height : input->width;
    float parentMain = frame.row ? input->parentWidth : input->parentHeight;
    float parentCross = frame.row ? input->parentHeight : input->parentWidth;
    frame.mainLimits = muiResolveAxis(&style->sizing, frame.row, parentMain);
    frame.crossLimits = muiResolveAxis(&style->sizing, !frame.row, parentCross);
    frame.gap = frame.row ? style->container.columnGap : style->container.rowGap;
    frame.crossGap = frame.row ? style->container.rowGap : style->container.columnGap;
    frame.multiLine = style->container.wrap != mui_wrapNone;
    frame.wrapReverse = style->container.wrap == mui_wrapReverse;
    frame.rtl = input->rtl;
    frame.extentMain = -1.0f;
    frame.extentCross = -1.0f;
    if (frame.mainIn.mode == mui_measureExact)
    {
        frame.innerMain = fmaxf(frame.mainIn.size - frame.boxMain, 0.0f);
        frame.extentMain = frame.innerMain;
    }
    if (frame.crossIn.mode == mui_measureExact)
    {
        frame.innerCross = fmaxf(frame.crossIn.size - frame.boxCross, 0.0f);
        frame.extentCross = frame.innerCross;
    }
    return frame;
}

// Section 9.2 and 9.3: every child's hypothetical main size, the
// container's inner main size from its content when not given, then the
// lines and each line's flexible lengths.
static void SizeMain(Frame* frame)
{
    const muiTree* tree = frame->solver->tree;
    float sum = 0.0f;
    float widest = 0.0f;
    for (uint32_t c = muiFirstFlowChild(tree, frame->solver->nodes, frame->node); c != 0;
         c = muiNextFlowChild(tree, frame->solver->nodes, c))
    {
        PrepareItem(frame, c);
        float outer = ItemOf(frame, c)->hypothetical + ItemOf(frame, c)->marginMain;
        sum += outer;
        widest = fmaxf(widest, outer);
        frame->count++;
    }
    if (frame->mainIn.mode != mui_measureExact)
    {
        // A container that wraps is at its narrowest one child per line.
        bool narrowest = frame->multiLine && frame->mainIn.mode == mui_measureMinContent;
        float content = narrowest ? widest : sum + Gaps(frame->gap, frame->count);
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
static float HypotheticalCross(const Frame* frame, uint32_t first, uint32_t count)
{
    float line = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = ItemOf(frame, c);
        muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        muiMeasureAxis constraint = CrossConstraint(frame, style, &cross, false);
        muiSizingInput input = ChildInput(frame, muiExact(item->target), constraint);
        float size = CrossOf(frame, frame->solver->solve(frame->solver, c, &input, false));
        item->cross =
            muiClampSize(size, item->minCross, item->maxCross, muiBoxSum(style, !frame->row));
        float outer = item->cross + item->marginCross;
        if (IsBaselineAligned(frame, style))
        {
            muiEdges margins = muiMarginsOf(style);
            muiSizingInput at = ChildInput(frame, muiExact(item->target), muiExact(item->cross));
            item->ascent =
                muiEdgeStart(&margins, false) + frame->solver->baseline(frame->solver, c, &at);
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
static float LineBaseline(const Frame* frame, uint32_t first, uint32_t count)
{
    float ascent = 0.0f;
    float descent = 0.0f;
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
static void Stretch(const Frame* frame, uint32_t first, uint32_t count)
{
    float line = ItemOf(frame, first)->lineCross;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(frame->solver->tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        muiFlexItemState* item = ItemOf(frame, c);
        muiAxisSizing cross = muiResolveAxis(&style->sizing, !frame->row, frame->extentCross);
        if (!cross.definite && IsStretched(frame, style))
        {
            item->cross = muiClampSize(line - item->marginCross, item->minCross, item->maxCross,
                                       muiBoxSum(style, !frame->row));
        }
    }
}

// Section 9.6 step 15: align-content places the lines in the free cross
// space and stretches them into it.
static void DistributeLines(const Frame* frame, float used)
{
    float lead = 0.0f;
    float between = 0.0f;
    float grow = 0.0f;
    muiAlignContentSpacing(frame->style->container.alignContent, frame->innerCross - used,
                           frame->lineCount, &lead, &between, &grow);
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
static void SizeCross(Frame* frame)
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
static float AutoCrossOffset(const Frame* frame, const muiLayoutStyle* style,
                             const muiFlexItemState* item, float line)
{
    muiEdges margins = muiMarginsOf(style);
    float start = muiEdgeStart(&margins, !frame->row);
    float freeSpace = line - item->cross - item->marginCross;
    bool autoStart = muiIsMarginAutoStart(style, !frame->row);
    bool autoEnd = muiIsMarginAutoEnd(style, !frame->row);
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
static float CrossOffset(const Frame* frame, const muiLayoutStyle* style,
                         const muiFlexItemState* item, float line, float baseline)
{
    if (muiIsMarginAutoStart(style, !frame->row) || muiIsMarginAutoEnd(style, !frame->row))
    {
        return AutoCrossOffset(frame, style, item, line);
    }
    muiEdges margins = muiMarginsOf(style);
    float start = muiEdgeStart(&margins, !frame->row);
    float end = muiEdgeEnd(&margins, !frame->row);
    muiAlign align = AlignOf(frame, style);
    if (align == mui_alignBaseline && frame->row)
    {
        // Section 9.6 step 14: the line's baseline-aligned children share
        // its baseline.
        return baseline - item->ascent + start;
    }
    if (align == mui_alignBaseline)
    {
        // In a column the cross axis is the inline axis: start.
        align = mui_alignStart;
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
static muiSizingInput FinalInput(const Frame* frame, uint32_t child)
{
    const muiFlexItemState* item = ItemOf(frame, child);
    muiSizingInput input = ChildInput(frame, muiExact(item->target), muiExact(item->cross));
    input.parentWidth = frame->row ? frame->innerMain : frame->innerCross;
    input.parentHeight = frame->row ? frame->innerCross : frame->innerMain;
    return input;
}

// Sets a child's rectangle from its main and cross offsets within the
// content box and lays it out in full.
static void PlaceChild(const Frame* frame, uint32_t child, float main, float cross)
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
static float LineStart(const Frame* frame, const muiFlexItemState* head)
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
static Offsets PlaceLine(const Frame* frame, uint32_t first, uint32_t count, uint32_t probe)
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
        autoMargins += (uint32_t)muiIsMarginAutoStart(style, frame->row) +
                       (uint32_t)muiIsMarginAutoEnd(style, frame->row);
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
        muiJustifySpacing(frame->style->container.justify, freeSpace, count, &lead, &between);
    }
    float lineStart = LineStart(frame, head);
    float baseline = head->lineBaselines ? LineBaseline(frame, first, count) : NAN;
    float flow = lead;
    for (uint32_t c = first, i = 0; i < count;
         c = muiNextFlowChild(tree, frame->solver->nodes, c), i++)
    {
        const muiLayoutStyle* style = &frame->solver->nodes[c - 1].style;
        const muiFlexItemState* item = ItemOf(frame, c);
        muiEdges margins = muiMarginsOf(style);
        bool autoStart = muiIsMarginAutoStart(style, frame->row);
        bool autoEnd = muiIsMarginAutoEnd(style, frame->row);
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
static Offsets Place(const Frame* frame, uint32_t line, uint32_t probe)
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
static float FirstBaseline(const Frame* frame)
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
    if (head->lineBaselines)
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
                    bool perform, float* baseline)
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
    Frame frame = Setup(solver, node, &fitted);
    SizeMain(&frame);
    SizeCross(&frame);
    if (baseline != nullptr)
    {
        *baseline = FirstBaseline(&frame);
    }
    else if (perform)
    {
        (void)Place(&frame, 0, 0);
    }
    float mainSize = frame.innerMain + frame.boxMain;
    float crossSize = frame.innerCross + frame.boxCross;
    return row ? (muiSize){mainSize, crossSize} : (muiSize){crossSize, mainSize};
}

muiSize muiLayoutFlex(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                      bool perform)
{
    return Flex(solver, node, input, perform, nullptr);
}

float muiFlexBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    float baseline = NAN;
    (void)Flex(solver, node, input, false, &baseline);
    return baseline;
}
