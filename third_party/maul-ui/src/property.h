// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Properties as data: each one's place in muiLayoutStyle,
// muiVisualStyle or muiTextStyle, its kind and the values it allows, so that checking,
// applying and comparing a set of them is one loop over a mask.

#ifndef MAUL_UI_SRC_PROPERTY_H
#define MAUL_UI_SRC_PROPERTY_H

#include "property_bits.h"

#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/style.h"
#include "maul-ui/text_style.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <stdbool.h>

// Every value a style can set.
typedef struct muiStyleValues
{
    muiLayoutStyle layout;
    muiVisualStyle visual;
    muiTextStyle text;
    muiInteractionStyle interaction;
} muiStyleValues;

// Values where they live: a node keeps its structs apart. A pointer may
// be NULL when the properties a call takes include none of its.
typedef struct muiValuesRef
{
    muiLayoutStyle* layout;
    muiVisualStyle* visual;
    muiTextStyle* text;
    muiInteractionStyle* interaction;
} muiValuesRef;

typedef struct muiConstValuesRef
{
    const muiLayoutStyle* layout;
    const muiVisualStyle* visual;
    const muiTextStyle* text;
    const muiInteractionStyle* interaction;
} muiConstValuesRef;

// A spec a variant gives a set of properties.
typedef struct muiTransitionBinding
{
    muiPropertyBits properties;
    muiTransitionId transition;
} muiTransitionBinding;

// Values for the properties named, the other fields unused; the
// properties that read a token instead, whose names are a list from the
// first name on, never the same property in both sets; and the
// transitions given to properties, no property in two bindings.
typedef struct muiPropertySet
{
    muiPropertyBits properties;
    muiStyleValues values;
    muiPropertyBits tokens;
    uint32_t firstTokenName;
    muiTransitionBinding bindings[MUI_MAX_VARIANT_TRANSITIONS];
    uint32_t bindingCount;
} muiPropertySet;

static inline muiValuesRef muiRefOf(muiStyleValues* values)
{
    return (muiValuesRef){&values->layout, &values->visual, &values->text, &values->interaction};
}

static inline muiConstValuesRef muiConstRefOf(const muiStyleValues* values)
{
    return (muiConstValuesRef){&values->layout, &values->visual, &values->text,
                               &values->interaction};
}

static inline muiConstValuesRef muiConstRef(muiValuesRef values)
{
    return (muiConstValuesRef){values.layout, values.visual, values.text, values.interaction};
}

// CSS's initial values, which muiDefaultLayoutStyle returns, and the
// visual, text and interaction defaults muiDefaultVisualStyle,
// muiDefaultTextStyle and muiDefaultInteractionStyle return.
const muiLayoutStyle* muiLayoutDefaults(void);
const muiVisualStyle* muiVisualDefaults(void);
const muiTextStyle* muiTextDefaults(void);
const muiInteractionStyle* muiInteractionDefaults(void);

// Whether an id names a property, and every property there is.
bool muiIsPropertyKnown(muiProperty property);
muiPropertyBits muiKnownProperties(void);

// Whether a group is one and a mask names only its known properties.
bool muiIsGroupMaskKnown(muiPropertyGroup group, muiPropertyMask mask);

// Whether every property named has a value in values it allows, and only
// known properties are named.
bool muiArePropertiesValid(muiConstValuesRef values, muiPropertyBits properties);

// Copies the properties named from source to target.
void muiApplyProperties(muiValuesRef target, muiConstValuesRef source, muiPropertyBits properties);

// Whether a property named has different values in a and b.
bool muiDoPropertiesDiffer(muiConstValuesRef a, muiConstValuesRef b, muiPropertyBits properties);

enum
{
    // The most channels a property moves through: a shadow's.
    MUI_MAX_CHANNELS = 8
};

// One property's value as it is stored, for the properties that move.
typedef struct muiPropertyValue
{
    alignas(8) unsigned char bytes[32];
} muiPropertyValue;

// The values a property moves through, in out: one for a number, two
// (scale, offset) for a Scale+Offset dimension or radius, four for insets
// or a color (premultiplied Oklab: lightness, a and b times alpha, and
// alpha), eight for a shadow (its color's four, then its offsets, blur
// and spread). Returns how many; 0 for a value that cannot move: an
// enumerator, a flag, a key, a gradient or an automatic dimension.
uint32_t muiPropertyChannels(muiConstValuesRef values, muiProperty property,
                             float out[MUI_MAX_CHANNELS]);

// Writes a property's channels, held to the values the property allows.
void muiSetPropertyChannels(muiValuesRef values, muiProperty property,
                            const float channels[MUI_MAX_CHANNELS]);

// Reads and writes a property that moves, exactly as it is stored.
void muiReadPropertyValue(muiConstValuesRef values, muiProperty property, muiPropertyValue* out);
void muiWritePropertyValue(muiValuesRef values, muiProperty property,
                           const muiPropertyValue* value);

// The type of token a property takes; 0 for none.
muiTokenType muiPropertyTokenType(muiProperty property);

// Whether a token's literal is valid for its type.
bool muiIsTokenValueValid(const muiTokenValue* value);

// Writes a token's value, of the property's type, to the property when
// the property allows it; false, writing nothing, when it does not.
bool muiApplyTokenValue(muiValuesRef values, muiProperty property, const muiTokenValue* value);

#endif // MAUL_UI_SRC_PROPERTY_H
