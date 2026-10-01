// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "condition.h"

#include <math.h>

#define VIEWPORTS (mui_viewportSmall | mui_viewportMedium | mui_viewportLarge)
#define INPUTS    (mui_inputPointer | mui_inputTouch | mui_inputGamepad)

#define WIDTH_PROPERTIES                                                                           \
    (MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyMinWidth) |                \
     MUI_PROPERTY_BIT(mui_propertyMaxWidth) | MUI_PROPERTY_BIT(mui_propertyAspectRatio))
#define HEIGHT_PROPERTIES                                                                          \
    (MUI_PROPERTY_BIT(mui_propertyHeight) | MUI_PROPERTY_BIT(mui_propertyMinHeight) |              \
     MUI_PROPERTY_BIT(mui_propertyMaxHeight) | MUI_PROPERTY_BIT(mui_propertyAspectRatio))

muiCondition muiDefaultCondition(void)
{
    const muiRange all = {0.0f, INFINITY};
    return (muiCondition){
        .width = all,
        .height = all,
        .aspect = all,
        .textScale = all,
        .viewports = VIEWPORTS,
        .inputs = INPUTS,
        .motion = mui_motionAny,
        .direction = mui_directionAny,
    };
}

muiEnvironment muiDefaultEnvironment(void)
{
    return (muiEnvironment){
        .viewport = mui_viewportMedium,
        .input = mui_inputPointer,
        .textScale = 1.0f,
        .reducedMotion = false,
    };
}

static bool IsRangeValid(muiRange range)
{
    return isfinite(range.min) && range.min >= 0.0f && !isnan(range.max) && range.max >= range.min;
}

// Whether a range is the default one, which holds for every value.
static bool IsEverything(muiRange range)
{
    return range.min == 0.0f && range.max == INFINITY;
}

// An unbounded range holds an infinite value too.
static bool IsIn(muiRange range, float value)
{
    return value >= range.min && (value < range.max || range.max == INFINITY);
}

// A single bit of mask.
static bool IsOneOf(uint8_t value, uint8_t mask)
{
    return value != 0 && (value & (value - 1)) == 0 && (value & ~mask) == 0;
}

bool muiIsConditionValid(const muiCondition* condition)
{
    return IsRangeValid(condition->width) && IsRangeValid(condition->height) &&
           IsRangeValid(condition->aspect) && IsRangeValid(condition->textScale) &&
           (condition->viewports & ~VIEWPORTS) == 0 && (condition->inputs & ~INPUTS) == 0 &&
           condition->motion <= mui_motionReduced &&
           condition->direction <= mui_directionRightToLeft;
}

bool muiIsEnvironmentValid(const muiEnvironment* environment)
{
    return IsOneOf(environment->viewport, VIEWPORTS) && IsOneOf(environment->input, INPUTS) &&
           isfinite(environment->textScale) && environment->textScale > 0.0f;
}

muiConditionReads muiConditionReadsOf(const muiCondition* condition)
{
    muiConditionReads reads = 0;
    if (!IsEverything(condition->width) || !IsEverything(condition->height) ||
        !IsEverything(condition->aspect))
    {
        reads |= mui_readsSize;
    }
    if (condition->direction != mui_directionAny)
    {
        reads |= mui_readsDirection;
    }
    return reads;
}

muiPropertyBits muiForbiddenProperties(const muiCondition* condition)
{
    // Layout properties alone.
    muiPropertyMask forbidden = 0;
    bool aspect = !IsEverything(condition->aspect);
    if (aspect || !IsEverything(condition->width))
    {
        forbidden |= WIDTH_PROPERTIES;
    }
    if (aspect || !IsEverything(condition->height))
    {
        forbidden |= HEIGHT_PROPERTIES;
    }
    if (condition->direction != mui_directionAny)
    {
        forbidden |= MUI_PROPERTY_BIT(mui_propertyTextDirection);
    }
    return muiPropertiesOf(mui_groupLayout, forbidden);
}

// Width over height: infinite for a zero height, 0 for an empty box.
static float AspectOf(float width, float height)
{
    if (height > 0.0f)
    {
        return width / height;
    }
    return width > 0.0f ? INFINITY : 0.0f;
}

static bool MatchesDirection(muiDirectionMatch match, bool rtl)
{
    return match == mui_directionAny || (match == mui_directionRightToLeft) == rtl;
}

static bool MatchesMotion(muiMotionMatch match, bool reduced)
{
    return match == mui_motionAny || (match == mui_motionReduced) == reduced;
}

bool muiConditionHolds(const muiCondition* condition, const muiConditionSample* sample)
{
    const muiEnvironment* environment = sample->environment;
    return IsIn(condition->width, sample->width) && IsIn(condition->height, sample->height) &&
           IsIn(condition->aspect, AspectOf(sample->width, sample->height)) &&
           IsIn(condition->textScale, environment->textScale) &&
           (condition->viewports & environment->viewport) != 0 &&
           (condition->inputs & environment->input) != 0 &&
           MatchesMotion(condition->motion, environment->reducedMotion) &&
           MatchesDirection(condition->direction, sample->rtl);
}
