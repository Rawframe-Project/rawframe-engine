// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text style (record mui-0004): the values a text service reads to lay
// out and draw a node's text, set through the same classes, variants,
// conditions, tokens, themes, transitions and direct writes as every
// property. They are inherited as CSS inherits them: a property no
// layer gives a node takes its parent's value, and a root the defaults.
// The core draws no text; a change marks a node with host content to be
// measured or painted again.

#ifndef MAUL_UI_TEXT_STYLE_H
#define MAUL_UI_TEXT_STYLE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // How upright the letters are.
    typedef uint8_t muiFontSlant;

    enum
    {
        mui_slantNormal = 0,
        mui_slantItalic = 1,
        mui_slantOblique = 2,
    };

    // Where lines sit across a node's content box: at the start, which
    // the text direction sets, the center or the end.
    typedef uint8_t muiTextAlign;

    enum
    {
        mui_textAlignStart = 0,
        mui_textAlignCenter = 1,
        mui_textAlignEnd = 2,
    };

    // Whether lines break to fit the content box's width.
    typedef uint8_t muiTextWrap;

    enum
    {
        mui_textWrap = 0,
        mui_textNoWrap = 1,
    };

    // Every text value a style can set. Build it with muiDefaultTextStyle.
    typedef struct muiTextStyle
    {
        // sRGB-encoded with straight alpha, as visual colors are.
        muiColor color;
        // A font key the text service gives out; 0 is its default font.
        uint64_t font;
        // Scale times the parent's computed size, plus offset, in logical
        // units: both 0 or more, as Scale+Offset (mui_dimensionValue).
        muiDimension size;
        // Automatic: the font's own line height. Otherwise scale times the
        // node's own size, plus offset, both 0 or more. Inherited as
        // written, so one ratio serves every size below.
        muiDimension lineHeight;
        // Added between letters: scale times the node's own size, plus
        // offset, either of which may be negative. Inherited as written.
        muiDimension letterSpacing;
        // From 1 to 1000; 400 is regular and 700 bold.
        float weight;
        muiFontSlant slant;
        muiTextAlign align;
        muiTextWrap wrap;
    } muiTextStyle;

    // A node's text values as inherited and resolved to logical units.
    typedef struct muiComputedTextStyle
    {
        muiColor color;
        uint64_t font;
        float size;
        // 0 when the line height is the font's own.
        float lineHeight;
        float letterSpacing;
        float weight;
        muiFontSlant slant;
        muiTextAlign align;
        muiTextWrap wrap;
        // Whether the line height is the font's own.
        bool automaticLineHeight;
    } muiComputedTextStyle;

    /// Returns the default text style: opaque black, font 0, 16 logical
    /// units, the font's own line height, no letter spacing, weight 400,
    /// upright, at the start, wrapping. A root takes these for what no
    /// layer gives it.
    ///
    /// @return The style.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiTextStyle muiDefaultTextStyle(void);

    /// Sets text values in one variant of a class, as
    /// muiStyle_SetLayoutValues sets layout ones.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; those mask names must be as
    ///                 muiTextStyle describes, and finite.
    /// @param mask     The properties, within MUI_TEXT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value
    ///         outside the above or a call from a measure or paint function, which
    ///         changes nothing; `mui_errorStale` for an id whose class is
    ///         gone; `mui_errorCapacity` when the variant had no values and
    ///         the context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetTextValues(muiContext* context, muiStyleId styleId,
                                                           muiVariant variant,
                                                           const muiTextStyle* values,
                                                           muiPropertyMask mask);

    /// Reads the text values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and muiDefaultTextStyle's
    ///                   for the rest.
    /// @param maskOut    Receives which properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetTextValues(const muiContext* context,
                                                           muiStyleId styleId, muiVariant variant,
                                                           muiTextStyle* valuesOut,
                                                           muiPropertyMask* maskOut);

    /// Writes text values to a node directly, over its classes, as
    /// muiNode_SetLayoutValues writes layout ones. Its children inherit
    /// them at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values; those mask names must be as
    ///                 muiTextStyle describes, and finite.
    /// @param mask     The properties, within MUI_TEXT_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure or paint function, which changes nothing;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetTextValues(muiContext* context, muiNodeId nodeId,
                                                          const muiTextStyle* values,
                                                          muiPropertyMask mask);

    /// Reads a node's computed text style, as of its last
    /// muiComputeLayout; a measure or paint function may read it.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param styleOut  Receives the computed values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetTextStyle(const muiContext* context,
                                                         muiNodeId nodeId,
                                                         muiComputedTextStyle* styleOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TEXT_STYLE_H
