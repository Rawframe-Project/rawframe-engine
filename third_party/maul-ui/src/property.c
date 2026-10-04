// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The property table: a table of rows per group, following the
// muiProperty numbering within it, which static assertions tie to the
// tables' lengths; each row names its struct, layout's or visual's, and
// its place there.

#include "property.h"

#include "color.h"
#include "invariant.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t Kind;

enum
{
    // A muiDimension.
    kindDimension,
    // A float: finite, finite and at least 0, or from 0 to 1.
    kindFinite,
    kindLength,
    kindFraction,
    // A uint8_t enumerator, or a bool, from low to high.
    kindEnum,
    // A muiColor.
    kindColor,
    // A muiGradient.
    kindGradient,
    // A Scale+Offset muiDimension of 0 or more.
    kindRadius,
    // A muiShadow.
    kindShadow,
    // A uint64_t host key: any value.
    kindKey,
    // A muiEdges of lengths.
    kindEdges,
    // A float from 1 to 1000: a font weight.
    kindWeight,
    // A muiDimension, automatic or Scale+Offset of 0 or more.
    kindAutoLength,
    // A Scale+Offset muiDimension, either part any finite number.
    kindSpacing,
};

typedef uint8_t Group;

enum
{
    groupLayout,
    groupVisual,
    groupText,
};

typedef struct Row
{
    uint16_t offset;
    // The field's size in bytes.
    uint8_t size;
    Kind kind;
    Group group;
    // For kindEnum, the values allowed.
    uint8_t low;
    uint8_t high;
} Row;

#define LAYOUT(field, type, kind, low, high)                                                       \
    {(uint16_t)offsetof(muiLayoutStyle, field), (uint8_t)sizeof(type), kind, groupLayout, low, high}
#define VISUAL(field, type, kind)                                                                  \
    {(uint16_t)offsetof(muiVisualStyle, field), (uint8_t)sizeof(type), kind, groupVisual, 0, 0}
#define TEXT(field, type, kind, low, high)                                                         \
    {(uint16_t)offsetof(muiTextStyle, field), (uint8_t)sizeof(type), kind, groupText, low, high}
#define DIMENSION(field)       LAYOUT(field, muiDimension, kindDimension, 0, 0)
#define NUMBER(field, kind)    LAYOUT(field, float, kind, 0, 0)
#define ENUM(field, low, high) LAYOUT(field, uint8_t, kindEnum, low, high)

static const Row s_layoutRows[] = {
    DIMENSION(sizing.width),
    DIMENSION(sizing.height),
    DIMENSION(sizing.minWidth),
    DIMENSION(sizing.minHeight),
    DIMENSION(sizing.maxWidth),
    DIMENSION(sizing.maxHeight),
    NUMBER(sizing.aspectRatio, kindLength),
    ENUM(container.direction, mui_flexRow, mui_flexColumnReverse),
    ENUM(container.wrap, mui_wrapNone, mui_wrapReverse),
    ENUM(container.justify, mui_justifyStart, mui_justifySpaceEvenly),
    // A container's alignment cannot be automatic.
    ENUM(container.alignItems, mui_alignStretch, mui_alignBaseline),
    ENUM(container.alignContent, mui_alignContentStretch, mui_alignContentSpaceEvenly),
    NUMBER(container.rowGap, kindLength),
    NUMBER(container.columnGap, kindLength),
    NUMBER(item.grow, kindLength),
    NUMBER(item.shrink, kindLength),
    DIMENSION(item.basis),
    ENUM(item.alignSelf, mui_alignAuto, mui_alignBaseline),
    NUMBER(margin.start, kindFinite),
    NUMBER(margin.end, kindFinite),
    NUMBER(margin.top, kindFinite),
    NUMBER(margin.bottom, kindFinite),
    ENUM(marginAuto, 0, mui_edgeStart | mui_edgeEnd | mui_edgeTop | mui_edgeBottom),
    NUMBER(border.start, kindLength),
    NUMBER(border.end, kindLength),
    NUMBER(border.top, kindLength),
    NUMBER(border.bottom, kindLength),
    NUMBER(padding.start, kindLength),
    NUMBER(padding.end, kindLength),
    NUMBER(padding.top, kindLength),
    NUMBER(padding.bottom, kindLength),
    ENUM(placement.position, mui_positionFlow, mui_positionAbsolute),
    DIMENSION(placement.inset.start),
    DIMENSION(placement.inset.end),
    DIMENSION(placement.inset.top),
    DIMENSION(placement.inset.bottom),
    NUMBER(placement.anchorX, kindFraction),
    NUMBER(placement.anchorY, kindFraction),
    ENUM(textDirection, mui_textInherit, mui_textRightToLeft),
    ENUM(content, mui_contentNone, mui_contentHost),
};

static const Row s_visualRows[] = {
    VISUAL(background, muiColor, kindColor),
    VISUAL(gradient, muiGradient, kindGradient),
    VISUAL(radius.topStart, muiDimension, kindRadius),
    VISUAL(radius.topEnd, muiDimension, kindRadius),
    VISUAL(radius.bottomEnd, muiDimension, kindRadius),
    VISUAL(radius.bottomStart, muiDimension, kindRadius),
    VISUAL(borderColor.start, muiColor, kindColor),
    VISUAL(borderColor.end, muiColor, kindColor),
    VISUAL(borderColor.top, muiColor, kindColor),
    VISUAL(borderColor.bottom, muiColor, kindColor),
    VISUAL(outerShadow, muiShadow, kindShadow),
    VISUAL(innerShadow, muiShadow, kindShadow),
    VISUAL(image, uint64_t, kindKey),
    VISUAL(imageSlice, muiEdges, kindEdges),
    VISUAL(imageTint, muiColor, kindColor),
    VISUAL(opacity, float, kindFraction),
    {(uint16_t)offsetof(muiVisualStyle, clip), (uint8_t)sizeof(bool), kindEnum, groupVisual, 0, 1},
};

static const Row s_textRows[] = {
    TEXT(color, muiColor, kindColor, 0, 0),
    TEXT(font, uint64_t, kindKey, 0, 0),
    TEXT(size, muiDimension, kindRadius, 0, 0),
    TEXT(lineHeight, muiDimension, kindAutoLength, 0, 0),
    TEXT(letterSpacing, muiDimension, kindSpacing, 0, 0),
    TEXT(weight, float, kindWeight, 0, 0),
    TEXT(slant, uint8_t, kindEnum, mui_slantNormal, mui_slantOblique),
    TEXT(align, uint8_t, kindEnum, mui_textAlignStart, mui_textAlignEnd),
    TEXT(wrap, uint8_t, kindEnum, mui_textWrap, mui_textNoWrap),
};

typedef struct GroupRows
{
    const Row* rows;
    uint32_t count;
} GroupRows;

static const GroupRows s_groups[MUI_PROPERTY_GROUPS] = {
    {s_layoutRows, (uint32_t)(sizeof s_layoutRows / sizeof s_layoutRows[0])},
    {s_visualRows, (uint32_t)(sizeof s_visualRows / sizeof s_visualRows[0])},
    {s_textRows, (uint32_t)(sizeof s_textRows / sizeof s_textRows[0])},
};

static_assert(sizeof s_layoutRows / sizeof s_layoutRows[0] == mui_propertyContent + 1 &&
                  sizeof s_visualRows / sizeof s_visualRows[0] == (mui_propertyClip & 63) + 1 &&
                  sizeof s_textRows / sizeof s_textRows[0] == (mui_propertyTextWrap & 63) + 1,
              "one row per property");
static_assert(MUI_PROPERTY_GROUP(mui_propertyTextColor) == mui_groupText &&
                  (mui_propertyTextColor & 63) == 0,
              "the text group starts at its first id");
static_assert(MUI_PROPERTY_GROUP(mui_propertyBackground) == mui_groupVisual &&
                  (mui_propertyBackground & 63) == 0,
              "the visual group starts at its first id");
static_assert(sizeof(muiGradient) <= UINT8_MAX, "sizes fit a row");
static_assert(sizeof(bool) == 1, "a flag is one byte, as an enumerator");
static_assert(sizeof(muiShadow) <= sizeof(muiPropertyValue), "a moving value fits");
static_assert(sizeof(muiGradient) >= sizeof(muiShadow) && sizeof(muiGradient) >= sizeof(muiColor),
              "the largest field a token feeds is a gradient");
static_assert(sizeof(muiColor) == 4 * sizeof(float) && sizeof(muiShadow) == 8 * sizeof(float) &&
                  sizeof(muiEdges) == 4 * sizeof(float) &&
                  sizeof(muiGradientStop) == 5 * sizeof(float),
              "compared as floats alone");
static_assert(MUI_LAYOUT_PROPERTIES == (MUI_PROPERTY_BIT(mui_propertyContent) << 1) - 1 &&
                  MUI_VISUAL_PROPERTIES == (MUI_PROPERTY_BIT(mui_propertyClip) << 1) - 1 &&
                  MUI_TEXT_PROPERTIES == (MUI_PROPERTY_BIT(mui_propertyTextWrap) << 1) - 1,
              "the masks name every property of their groups");

// A known property's row.
static const Row* RowOf(muiProperty property)
{
    return &s_groups[MUI_PROPERTY_GROUP(property)].rows[property & 63];
}

bool muiIsPropertyKnown(muiProperty property)
{
    return (property & 63u) < s_groups[MUI_PROPERTY_GROUP(property)].count;
}

static const muiPropertyBits s_known = {
    {MUI_LAYOUT_PROPERTIES, MUI_VISUAL_PROPERTIES, MUI_TEXT_PROPERTIES}};

bool muiIsGroupMaskKnown(muiPropertyGroup group, muiPropertyMask mask)
{
    return group < MUI_PROPERTY_GROUPS && (mask & ~s_known.words[group]) == 0;
}

muiPropertyBits muiKnownProperties(void)
{
    return s_known;
}

// The lowest bit of a word that is not 0, by a de Bruijn sequence.
static uint32_t LowestBit(uint64_t word)
{
    static const uint8_t index[64] = {
        0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,  62, 55, 59, 36, 53, 51,
        43, 22, 45, 39, 33, 30, 24, 18, 12, 5,  63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21,
        44, 32, 23, 11, 46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
    return index[((word & (~word + 1)) * 0x03F79D71B4CB0A89ull) >> 58];
}

static const muiLayoutStyle s_layoutDefaults = {
    .container = {.direction = mui_flexRow,
                  .wrap = mui_wrapNone,
                  .justify = mui_justifyStart,
                  .alignItems = mui_alignStretch,
                  .alignContent = mui_alignContentStretch},
    .item = {.shrink = 1.0f, .alignSelf = mui_alignAuto},
};

static const muiVisualStyle s_visualDefaults = {
    .radius = {{0.0f, 0.0f, mui_dimensionValue},
               {0.0f, 0.0f, mui_dimensionValue},
               {0.0f, 0.0f, mui_dimensionValue},
               {0.0f, 0.0f, mui_dimensionValue}},
    .borderColor = {{0.0f, 0.0f, 0.0f, 1.0f},
                    {0.0f, 0.0f, 0.0f, 1.0f},
                    {0.0f, 0.0f, 0.0f, 1.0f},
                    {0.0f, 0.0f, 0.0f, 1.0f}},
    .imageTint = {1.0f, 1.0f, 1.0f, 1.0f},
    .opacity = 1.0f,
};

const muiLayoutStyle* muiLayoutDefaults(void)
{
    return &s_layoutDefaults;
}

const muiVisualStyle* muiVisualDefaults(void)
{
    return &s_visualDefaults;
}

// CSS's medium size and normal weight, black, and the font's own line
// height.
static const muiTextStyle s_textDefaults = {
    .color = {0.0f, 0.0f, 0.0f, 1.0f},
    .size = {0.0f, 16.0f, mui_dimensionValue},
    .lineHeight = {0.0f, 0.0f, mui_dimensionAuto},
    .letterSpacing = {0.0f, 0.0f, mui_dimensionValue},
    .weight = 400.0f,
    .slant = mui_slantNormal,
    .align = mui_textAlignStart,
    .wrap = mui_textWrap,
};

const muiTextStyle* muiTextDefaults(void)
{
    return &s_textDefaults;
}

muiTextStyle muiDefaultTextStyle(void)
{
    return s_textDefaults;
}

// The index of the lowest set bit of a mask that is not 0, by a de Bruijn
// sequence: portable, and the same on every compiler.
static const void* At(muiConstValuesRef values, const Row* row)
{
    const void* base = row->group == groupLayout   ? (const void*)values.layout
                       : row->group == groupVisual ? (const void*)values.visual
                                                   : (const void*)values.text;
    return (const unsigned char*)base + row->offset;
}

static void* AtMutable(muiValuesRef values, const Row* row)
{
    void* base = row->group == groupLayout   ? (void*)values.layout
                 : row->group == groupVisual ? (void*)values.visual
                                             : (void*)values.text;
    return (unsigned char*)base + row->offset;
}

static float NumberAt(muiConstValuesRef values, const Row* row)
{
    float value = 0.0f;
    memcpy(&value, At(values, row), sizeof value);
    return value;
}

static muiDimension DimensionAt(muiConstValuesRef values, const Row* row)
{
    muiDimension value = {0};
    memcpy(&value, At(values, row), sizeof value);
    return value;
}

static bool IsUnit(float value)
{
    return value >= 0.0f && value <= 1.0f;
}

static bool IsLength(float value)
{
    return isfinite(value) && value >= 0.0f;
}

static bool IsColorValid(muiColor color)
{
    return IsUnit(color.r) && IsUnit(color.g) && IsUnit(color.b) && IsUnit(color.a);
}

static bool IsDimensionValid(muiDimension value)
{
    return value.kind <= mui_dimensionValue && isfinite(value.scale) && isfinite(value.offset);
}

// None with no stops, or a linear or radial gradient with 2 or more
// stops in order from 0 to 1.
static bool IsGradientValid(const muiGradient* gradient)
{
    if (gradient->kind == mui_gradientNone)
    {
        return gradient->stopCount == 0;
    }
    if (gradient->kind > mui_gradientRadial || gradient->stopCount < 2 ||
        gradient->stopCount > MUI_MAX_GRADIENT_STOPS || !isfinite(gradient->angle))
    {
        return false;
    }
    float previous = 0.0f;
    for (uint32_t i = 0; i < gradient->stopCount; i++)
    {
        const muiGradientStop* stop = &gradient->stops[i];
        if (!IsColorValid(stop->color) || !IsUnit(stop->position) || stop->position < previous)
        {
            return false;
        }
        previous = stop->position;
    }
    return true;
}

static bool IsShadowValid(const muiShadow* shadow)
{
    return IsColorValid(shadow->color) && isfinite(shadow->offsetX) && isfinite(shadow->offsetY) &&
           IsLength(shadow->blur) && isfinite(shadow->spread);
}

// The visual kinds, which a struct of their own holds.
static bool IsCompoundValid(muiConstValuesRef values, const Row* row)
{
    const void* at = At(values, row);
    switch (row->kind)
    {
    case kindColor:
    {
        muiColor color;
        memcpy(&color, at, sizeof color);
        return IsColorValid(color);
    }
    case kindGradient:
    {
        muiGradient gradient;
        memcpy(&gradient, at, sizeof gradient);
        return IsGradientValid(&gradient);
    }
    case kindShadow:
    {
        muiShadow shadow;
        memcpy(&shadow, at, sizeof shadow);
        return IsShadowValid(&shadow);
    }
    case kindEdges:
    {
        muiEdges edges;
        memcpy(&edges, at, sizeof edges);
        return IsLength(edges.start) && IsLength(edges.end) && IsLength(edges.top) &&
               IsLength(edges.bottom);
    }
    default:
        // A host key: any value.
        return true;
    }
}

static bool IsValid(muiConstValuesRef values, const Row* row)
{
    switch (row->kind)
    {
    case kindDimension:
        return IsDimensionValid(DimensionAt(values, row));
    case kindRadius:
    {
        // A corner has no automatic radius.
        muiDimension value = DimensionAt(values, row);
        return value.kind == mui_dimensionValue && IsLength(value.scale) && IsLength(value.offset);
    }
    case kindAutoLength:
    {
        muiDimension value = DimensionAt(values, row);
        return value.kind == mui_dimensionAuto
                   ? value.scale == 0.0f && value.offset == 0.0f
                   : value.kind == mui_dimensionValue && IsLength(value.scale) &&
                         IsLength(value.offset);
    }
    case kindSpacing:
    {
        muiDimension value = DimensionAt(values, row);
        return value.kind == mui_dimensionValue && isfinite(value.scale) && isfinite(value.offset);
    }
    case kindWeight:
    {
        float value = NumberAt(values, row);
        return value >= 1.0f && value <= 1000.0f;
    }
    case kindFinite:
        return isfinite(NumberAt(values, row));
    case kindLength:
        return IsLength(NumberAt(values, row));
    case kindFraction:
        return IsUnit(NumberAt(values, row));
    case kindEnum:
    {
        uint8_t value = *(const uint8_t*)At(values, row);
        return value >= row->low && value <= row->high;
    }
    default:
        return IsCompoundValid(values, row);
    }
}

// Whether count floats at a and b are equal by value, as 0 and -0 are.
static bool AreFloatsEqual(const void* a, const void* b, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        float x = 0.0f;
        float y = 0.0f;
        memcpy(&x, (const float*)a + i, sizeof x);
        memcpy(&y, (const float*)b + i, sizeof y);
        if (x != y)
        {
            return false;
        }
    }
    return true;
}

static bool AreGradientsEqual(const muiGradient* a, const muiGradient* b)
{
    if (a->kind != b->kind || a->stopCount != b->stopCount || a->angle != b->angle)
    {
        return false;
    }
    for (uint32_t i = 0; i < a->stopCount && i < MUI_MAX_GRADIENT_STOPS; i++)
    {
        if (!AreFloatsEqual(&a->stops[i], &b->stops[i], sizeof(muiGradientStop) / sizeof(float)))
        {
            return false;
        }
    }
    return true;
}

static bool AreEqual(muiConstValuesRef a, muiConstValuesRef b, const Row* row)
{
    switch (row->kind)
    {
    case kindDimension:
    case kindRadius:
    case kindAutoLength:
    case kindSpacing:
    {
        // Compared by field: a dimension has padding bytes.
        muiDimension x = DimensionAt(a, row);
        muiDimension y = DimensionAt(b, row);
        return x.kind == y.kind && x.scale == y.scale && x.offset == y.offset;
    }
    case kindGradient:
    {
        muiGradient x;
        muiGradient y;
        memcpy(&x, At(a, row), sizeof x);
        memcpy(&y, At(b, row), sizeof y);
        return AreGradientsEqual(&x, &y);
    }
    case kindEnum:
    case kindKey:
        return memcmp(At(a, row), At(b, row), row->size) == 0;
    default:
        // A number, or a struct of floats alone.
        return AreFloatsEqual(At(a, row), At(b, row), row->size / (uint32_t)sizeof(float));
    }
}

bool muiArePropertiesValid(muiConstValuesRef values, muiPropertyBits properties)
{
    if (muiAnyProperty(muiWithout(properties, s_known)))
    {
        return false;
    }
    for (uint32_t group = 0; group < MUI_PROPERTY_GROUPS; group++)
    {
        const Row* rows = s_groups[group].rows;
        for (uint64_t left = properties.words[group]; left != 0; left &= left - 1)
        {
            if (!IsValid(values, &rows[LowestBit(left)]))
            {
                return false;
            }
        }
    }
    return true;
}

// Copies one property.
static void ApplyRow(muiValuesRef target, muiConstValuesRef source, const Row* row)
{
    // Constant sizes for layout's kinds, so each copy compiles to a move.
    switch (row->kind)
    {
    case kindDimension:
        memcpy(AtMutable(target, row), At(source, row), sizeof(muiDimension));
        break;
    case kindEnum:
        memcpy(AtMutable(target, row), At(source, row), sizeof(uint8_t));
        break;
    case kindFinite:
    case kindLength:
    case kindFraction:
        memcpy(AtMutable(target, row), At(source, row), sizeof(float));
        break;
    default:
        memcpy(AtMutable(target, row), At(source, row), row->size);
        break;
    }
}

void muiApplyProperties(muiValuesRef target, muiConstValuesRef source, muiPropertyBits properties)
{
    for (uint32_t group = 0; group < MUI_PROPERTY_GROUPS; group++)
    {
        const Row* rows = s_groups[group].rows;
        for (uint64_t left = properties.words[group]; left != 0; left &= left - 1)
        {
            ApplyRow(target, source, &rows[LowestBit(left)]);
        }
    }
}

bool muiDoPropertiesDiffer(muiConstValuesRef a, muiConstValuesRef b, muiPropertyBits properties)
{
    for (uint32_t group = 0; group < MUI_PROPERTY_GROUPS; group++)
    {
        const Row* rows = s_groups[group].rows;
        for (uint64_t left = properties.words[group]; left != 0; left &= left - 1)
        {
            if (!AreEqual(a, b, &rows[LowestBit(left)]))
            {
                return true;
            }
        }
    }
    return false;
}

// The channels of n lengths of 0 or more, or of n finite numbers.
static void ReadFloats(const void* at, uint32_t n, float out[])
{
    memcpy(out, at, n * sizeof(float));
}

static uint32_t ShadowChannels(const void* at, float out[MUI_MAX_CHANNELS])
{
    muiShadow shadow;
    memcpy(&shadow, at, sizeof shadow);
    muiColorToChannels(shadow.color, out);
    out[4] = shadow.offsetX;
    out[5] = shadow.offsetY;
    out[6] = shadow.blur;
    out[7] = shadow.spread;
    return 8;
}

uint32_t muiPropertyChannels(muiConstValuesRef values, muiProperty property,
                             float out[MUI_MAX_CHANNELS])
{
    const Row* row = RowOf(property);
    const void* at = At(values, row);
    switch (row->kind)
    {
    case kindDimension:
    case kindRadius:
    case kindAutoLength:
    case kindSpacing:
    {
        muiDimension value = DimensionAt(values, row);
        if (value.kind != mui_dimensionValue)
        {
            return 0;
        }
        out[0] = value.scale;
        out[1] = value.offset;
        return 2;
    }
    case kindFinite:
    case kindLength:
    case kindFraction:
    case kindWeight:
        ReadFloats(at, 1, out);
        return 1;
    case kindEdges:
        ReadFloats(at, 4, out);
        return 4;
    case kindColor:
    {
        muiColor color;
        memcpy(&color, at, sizeof color);
        muiColorToChannels(color, out);
        return 4;
    }
    case kindShadow:
        return ShadowChannels(at, out);
    default:
        return 0;
    }
}

static void SetShadow(void* at, const float channels[MUI_MAX_CHANNELS])
{
    const muiShadow shadow = {muiColorFromChannels(channels), channels[4], channels[5],
                              fmaxf(channels[6], 0.0f), channels[7]};
    memcpy(at, &shadow, sizeof shadow);
}

void muiSetPropertyChannels(muiValuesRef values, muiProperty property,
                            const float channels[MUI_MAX_CHANNELS])
{
    const Row* row = RowOf(property);
    void* at = AtMutable(values, row);
    switch (row->kind)
    {
    case kindDimension:
    case kindSpacing:
    {
        const muiDimension value = {channels[0], channels[1], mui_dimensionValue};
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindRadius:
    case kindAutoLength:
    {
        const muiDimension value = {fmaxf(channels[0], 0.0f), fmaxf(channels[1], 0.0f),
                                    mui_dimensionValue};
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindLength:
    {
        // A spring can overshoot below 0.
        const float value = fmaxf(channels[0], 0.0f);
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindFraction:
    {
        const float value = fminf(fmaxf(channels[0], 0.0f), 1.0f);
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindWeight:
    {
        const float value = fminf(fmaxf(channels[0], 1.0f), 1000.0f);
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindFinite:
        memcpy(at, &channels[0], sizeof channels[0]);
        break;
    case kindEdges:
    {
        const muiEdges value = {fmaxf(channels[0], 0.0f), fmaxf(channels[1], 0.0f),
                                fmaxf(channels[2], 0.0f), fmaxf(channels[3], 0.0f)};
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindColor:
    {
        const muiColor value = muiColorFromChannels(channels);
        memcpy(at, &value, sizeof value);
        break;
    }
    case kindShadow:
        SetShadow(at, channels);
        break;
    default:
        break;
    }
}

void muiReadPropertyValue(muiConstValuesRef values, muiProperty property, muiPropertyValue* out)
{
    const Row* row = RowOf(property);
    MUI_ASSERT(row->size <= sizeof out->bytes);
    memcpy(out->bytes, At(values, row), row->size);
}

void muiWritePropertyValue(muiValuesRef values, muiProperty property, const muiPropertyValue* value)
{
    const Row* row = RowOf(property);
    MUI_ASSERT(row->size <= sizeof value->bytes);
    memcpy(AtMutable(values, row), value->bytes, row->size);
}

muiTokenType muiPropertyTokenType(muiProperty property)
{
    switch (RowOf(property)->kind)
    {
    case kindColor:
        return mui_tokenColor;
    case kindFinite:
    case kindLength:
    case kindFraction:
    case kindWeight:
        return mui_tokenNumber;
    case kindDimension:
    case kindRadius:
    case kindAutoLength:
    case kindSpacing:
        return mui_tokenDimension;
    case kindShadow:
        return mui_tokenShadow;
    case kindGradient:
        return mui_tokenGradient;
    default:
        return 0;
    }
}

bool muiIsTokenValueValid(const muiTokenValue* value)
{
    switch (value->type)
    {
    case mui_tokenColor:
        return IsColorValid(value->color);
    case mui_tokenNumber:
        return isfinite(value->number);
    case mui_tokenDimension:
        return IsDimensionValid(value->dimension);
    case mui_tokenShadow:
        return IsShadowValid(&value->shadow);
    case mui_tokenGradient:
        return IsGradientValid(&value->gradient);
    default:
        return false;
    }
}

// The bytes of a token's member, which have the size of the property's
// field for a property of the token's type.
static const void* MemberOf(const muiTokenValue* value)
{
    switch (value->type)
    {
    case mui_tokenColor:
        return &value->color;
    case mui_tokenNumber:
        return &value->number;
    case mui_tokenDimension:
        return &value->dimension;
    case mui_tokenShadow:
        return &value->shadow;
    default:
        return &value->gradient;
    }
}

bool muiApplyTokenValue(muiValuesRef values, muiProperty property, const muiTokenValue* value)
{
    const Row* row = RowOf(property);
    MUI_ASSERT(muiPropertyTokenType(property) == value->type);
    // Written, checked against the property's own rule, and put back when
    // the property does not allow it.
    unsigned char kept[sizeof(muiGradient)];
    void* at = AtMutable(values, row);
    memcpy(kept, at, row->size);
    memcpy(at, MemberOf(value), row->size);
    if (IsValid(muiConstRef(values), row))
    {
        return true;
    }
    memcpy(at, kept, row->size);
    return false;
}
