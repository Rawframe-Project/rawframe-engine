// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The loop of CSS Flexbox section 9.7: distribute free space by the flex
// factors, clamp to the limits, freeze the items that violated a limit
// in the direction of the total violation, and repeat. Sums run in child
// order so the result is the same everywhere.

#include "flex_resolve.h"

#include <math.h>

static float Outer(const muiFlexItemState* item, float size)
{
    return size + item->marginMain;
}

// Freezes the inflexible children at their hypothetical size and returns
// the initial free space.
static float FreezeInflexible(const muiTree* tree, muiLayoutNode* nodes, uint32_t first,
                              uint32_t count, float innerMain, bool growing)
{
    float used = 0.0f;
    for (uint32_t c = first, i = 0; i < count; c = muiNextFlowChild(tree, nodes, c), i++)
    {
        muiFlexItemState* item = &nodes[c - 1].item;
        const muiFlexItem* flex = &nodes[c - 1].style.item;
        float factor = growing ? flex->grow : flex->shrink;
        item->frozen = factor == 0.0f || (growing && item->base > item->hypothetical) ||
                       (!growing && item->base < item->hypothetical);
        item->target = item->hypothetical;
        item->violation = 0;
        used += Outer(item, item->frozen ? item->target : item->base);
    }
    return innerMain - used;
}

typedef struct FlexTotals
{
    // Free space left with the unfrozen children at their base size.
    float remaining;
    // The unfrozen children's flex factors (grow or shrink), and their
    // shrink factors scaled by their inner base size.
    float factors;
    float scaledShrink;
    bool anyUnfrozen;
} FlexTotals;

static FlexTotals SumUnfrozen(const muiTree* tree, const muiLayoutNode* nodes, uint32_t first,
                              uint32_t count, float innerMain, bool growing)
{
    FlexTotals totals = {.remaining = innerMain};
    for (uint32_t c = first, i = 0; i < count; c = muiNextFlowChild(tree, nodes, c), i++)
    {
        const muiFlexItemState* item = &nodes[c - 1].item;
        const muiFlexItem* flex = &nodes[c - 1].style.item;
        if (item->frozen)
        {
            totals.remaining -= Outer(item, item->target);
            continue;
        }
        totals.anyUnfrozen = true;
        totals.remaining -= Outer(item, item->base);
        totals.factors += growing ? flex->grow : flex->shrink;
        totals.scaledShrink += flex->shrink * item->innerBase;
    }
    return totals;
}

// Gives the unfrozen children their share of freeSpace, clamps them to
// their limits, and returns the total violation.
static float Distribute(const muiTree* tree, muiLayoutNode* nodes, uint32_t first, uint32_t count,
                        float freeSpace, bool growing, const FlexTotals* totals)
{
    float violation = 0.0f;
    for (uint32_t c = first, i = 0; i < count; c = muiNextFlowChild(tree, nodes, c), i++)
    {
        muiFlexItemState* item = &nodes[c - 1].item;
        const muiFlexItem* flex = &nodes[c - 1].style.item;
        if (item->frozen)
        {
            continue;
        }
        float target = item->base;
        if (growing && totals->factors > 0.0f)
        {
            target = item->base + freeSpace * (flex->grow / totals->factors);
        }
        else if (!growing && totals->scaledShrink > 0.0f)
        {
            float ratio = flex->shrink * item->innerBase / totals->scaledShrink;
            target = item->base - fabsf(freeSpace) * ratio;
        }
        // The minimum wins over the maximum, as in CSS.
        float clamped = fmaxf(item->minMain, fminf(target, item->maxMain));
        item->violation = clamped > target ? 1 : (clamped < target ? -1 : 0);
        violation += clamped - target;
        item->target = clamped;
    }
    return violation;
}

// Freezes every unfrozen child when the total violation is zero, the
// ones raised to their minimum when it is positive, and the ones lowered
// to their maximum when it is negative.
static void FreezeViolators(const muiTree* tree, muiLayoutNode* nodes, uint32_t first,
                            uint32_t count, float violation)
{
    int8_t sign = violation > 0.0f ? 1 : (violation < 0.0f ? -1 : 0);
    for (uint32_t c = first, i = 0; i < count; c = muiNextFlowChild(tree, nodes, c), i++)
    {
        muiFlexItemState* item = &nodes[c - 1].item;
        if (!item->frozen && (sign == 0 || item->violation == sign))
        {
            item->frozen = true;
        }
    }
}

uint32_t muiCollectLine(const muiTree* tree, const muiLayoutNode* nodes, uint32_t first,
                        float innerMain, float gap, bool wrap)
{
    uint32_t count = 0;
    float used = 0.0f;
    for (uint32_t c = first; c != 0; c = muiNextFlowChild(tree, nodes, c))
    {
        float outer = Outer(&nodes[c - 1].item, nodes[c - 1].item.hypothetical);
        float next = count == 0 ? outer : used + gap + outer;
        if (wrap && count > 0 && next > innerMain)
        {
            break;
        }
        used = next;
        count++;
    }
    return count;
}

void muiResolveFlexibleLengths(const muiTree* tree, muiLayoutNode* nodes, uint32_t first,
                               uint32_t count, float innerMain, float gaps)
{
    float available = innerMain - gaps;
    float hypotheticalSum = 0.0f;
    for (uint32_t c = first, i = 0; i < count; c = muiNextFlowChild(tree, nodes, c), i++)
    {
        hypotheticalSum += Outer(&nodes[c - 1].item, nodes[c - 1].item.hypothetical);
    }
    bool growing = hypotheticalSum < available;
    float initialFree = FreezeInflexible(tree, nodes, first, count, available, growing);
    for (;;)
    {
        FlexTotals totals = SumUnfrozen(tree, nodes, first, count, available, growing);
        if (!totals.anyUnfrozen)
        {
            return;
        }
        float freeSpace = totals.remaining;
        if (totals.factors < 1.0f)
        {
            float scaled = initialFree * totals.factors;
            if (fabsf(scaled) < fabsf(freeSpace))
            {
                freeSpace = scaled;
            }
        }
        float violation = Distribute(tree, nodes, first, count, freeSpace, growing, &totals);
        FreezeViolators(tree, nodes, first, count, violation);
    }
}

// Where overflowing content goes under safe alignment: to the writing
// mode's start (CSS Box Alignment), the far end of a reversed axis.
static float SafeLead(float freeSpace, bool reversed)
{
    return reversed ? fminf(freeSpace, 0.0f) : 0.0f;
}

void muiJustifySpacing(muiJustify justify, float freeSpace, uint32_t count, bool reversed,
                       float* leadOut, float* betweenOut)
{
    float lead = 0.0f;
    float between = 0.0f;
    float positive = fmaxf(freeSpace, 0.0f);
    switch (justify)
    {
    case mui_justifyEnd:
        lead = freeSpace;
        break;
    case mui_justifyCenter:
        lead = freeSpace / 2.0f;
        break;
    case mui_justifySpaceBetween:
        between = count > 1 ? positive / (float)(count - 1) : 0.0f;
        break;
    case mui_justifySpaceAround:
        // Overflow packs at the start: the distributed values fall back to
        // safe centring (CSS Box Alignment), which never pushes content
        // past the writing mode's start.
        between = count > 0 ? positive / (float)count : 0.0f;
        lead = between / 2.0f + SafeLead(freeSpace, reversed);
        break;
    case mui_justifySpaceEvenly:
        between = positive / (float)(count + 1);
        lead = between + SafeLead(freeSpace, reversed);
        break;
    default:
        break;
    }
    *leadOut = lead;
    *betweenOut = between;
}

void muiAlignContentSpacing(muiAlignContent align, float freeSpace, uint32_t count, bool reversed,
                            float* leadOut, float* betweenOut, float* growOut)
{
    float lead = 0.0f;
    float between = 0.0f;
    float grow = 0.0f;
    float positive = fmaxf(freeSpace, 0.0f);
    switch (align)
    {
    case mui_alignContentStretch:
        grow = count > 0 ? positive / (float)count : 0.0f;
        break;
    case mui_alignContentEnd:
        lead = freeSpace;
        break;
    case mui_alignContentCenter:
        lead = freeSpace / 2.0f;
        break;
    case mui_alignContentSpaceBetween:
        between = count > 1 ? positive / (float)(count - 1) : 0.0f;
        break;
    case mui_alignContentSpaceAround:
        between = count > 0 ? positive / (float)count : 0.0f;
        lead = between / 2.0f + SafeLead(freeSpace, reversed);
        break;
    case mui_alignContentSpaceEvenly:
        between = positive / (float)(count + 1);
        lead = between + SafeLead(freeSpace, reversed);
        break;
    default:
        break;
    }
    *leadOut = lead;
    *betweenOut = between;
    *growOut = grow;
}
