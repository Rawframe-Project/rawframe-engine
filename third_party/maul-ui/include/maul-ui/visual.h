// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Visual values: what the draw-command list paints for a node, set
// through the same classes, variants, conditions, transitions and direct
// writes as layout values (record mui-0004). A change to one marks the
// node's paint, never its layout.

#ifndef MAUL_UI_VISUAL_H
#define MAUL_UI_VISUAL_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/style.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A color: sRGB-encoded red, green and blue, and straight (not
    // premultiplied) alpha, each from 0 to 1.
    typedef struct muiColor
    {
        float r;
        float g;
        float b;
        float a;
    } muiColor;

    // How a gradient spreads its stops.
    typedef uint8_t muiGradientKind;

    enum
    {
        mui_gradientNone = 0,
        // Along a line through the centre at the angle.
        mui_gradientLinear = 1,
        // Outward from the centre, as CSS's ellipse to the farthest corner.
        mui_gradientRadial = 2,
    };

    enum
    {
        // The stops a gradient holds.
        MUI_MAX_GRADIENT_STOPS = 4
    };

    // A color at a position along a gradient, from 0 to 1.
    typedef struct muiGradientStop
    {
        muiColor color;
        float position;
    } muiGradientStop;

    // A gradient, painted over the background color.
    typedef struct muiGradient
    {
        muiGradientKind kind;
        // From 2 to MUI_MAX_GRADIENT_STOPS for a gradient, 0 for none.
        uint8_t stopCount;
        // For a linear gradient, in degrees clockwise from toward the top,
        // as CSS's linear-gradient.
        float angle;
        // In order of position.
        muiGradientStop stops[MUI_MAX_GRADIENT_STOPS];
    } muiGradient;

    // The radius of each corner, in logical order: start and end follow
    // the inline direction. Each is Scale+Offset (mui_dimensionValue);
    // scale applies to the shorter side of the border box, and a radius is
    // held to half of that side, so a large one makes a pill.
    typedef struct muiCornerRadii
    {
        muiDimension topStart;
        muiDimension topEnd;
        muiDimension bottomEnd;
        muiDimension bottomStart;
    } muiCornerRadii;

    // A color per side, in logical order.
    typedef struct muiEdgeColors
    {
        muiColor start;
        muiColor end;
        muiColor top;
        muiColor bottom;
    } muiEdgeColors;

    // A shadow, as CSS's box-shadow: offset right and down, blurred over
    // blur, grown by spread. A clear color draws none.
    typedef struct muiShadow
    {
        muiColor color;
        float offsetX;
        float offsetY;
        float blur;
        float spread;
    } muiShadow;

    // Every visual value. Build it with muiDefaultVisualStyle.
    typedef struct muiVisualStyle
    {
        muiColor background;
        muiGradient gradient;
        muiCornerRadii radius;
        // The border's colors; its widths are layout's (muiLayoutStyle).
        muiEdgeColors borderColor;
        // Outside the border box, and inside the padding box.
        muiShadow outerShadow;
        muiShadow innerShadow;
        // The host's key for an image filling the border box; 0 for none.
        uint64_t image;
        // Insets of the image's 9-slice centre, in its pixels; all 0
        // stretches the whole image. An image does not mirror: start and
        // end are its left and right.
        muiEdges imageSlice;
        // Multiplies the image's colors.
        muiColor imageTint;
        // The node and its subtree, from 0 to 1.
        float opacity;
        // Whether the node clips its children to its rounded border box.
        bool clip;
    } muiVisualStyle;

    /// Returns the default visual style: clear background, no gradient,
    /// square corners, opaque black border colors, no shadows or image, a
    /// white tint, opacity 1 and no clipping.
    ///
    /// @return The values.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiVisualStyle muiDefaultVisualStyle(void);

    /// Sets visual properties in one variant of a class from the fields of
    /// values, as muiStyle_SetLayoutValues does layout ones.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; only the fields mask names are read:
    ///                 colors with components from 0 to 1, a gradient of a
    ///                 known kind with 2 to MUI_MAX_GRADIENT_STOPS stops in
    ///                 order from 0 to 1 (or none with 0), Scale+Offset
    ///                 radii and slice insets of 0 or more, shadows with finite offsets and
    ///                 spread and a blur of 0 or more, opacity from 0 to 1.
    /// @param mask     The properties, within MUI_VISUAL_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value outside
    ///         the above or a call from a measure or paint function, which changes
    ///         nothing; `mui_errorStale` for an id whose class is gone;
    ///         `mui_errorCapacity` when the variant had no values and the
    ///         context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetVisualValues(muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             const muiVisualStyle* values,
                                                             muiPropertyMask mask);

    /// Reads the visual values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and muiDefaultVisualStyle's
    ///                   for the rest.
    /// @param maskOut    Receives which visual properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetVisualValues(const muiContext* context,
                                                             muiStyleId styleId, muiVariant variant,
                                                             muiVisualStyle* valuesOut,
                                                             muiPropertyMask* maskOut);

    /// Writes visual properties of a node directly, as
    /// muiNode_SetLayoutValues does layout ones; the node's paint, not its
    /// layout, is redone.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values, as muiStyle_SetVisualValues takes them.
    /// @param mask     The properties, within MUI_VISUAL_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure or paint function, which changes nothing;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetVisualValues(muiContext* context, muiNodeId nodeId,
                                                            const muiVisualStyle* values,
                                                            muiPropertyMask mask);

    /// Reads a node's resolved visual values: its direct writes, and for the
    /// other properties what its classes and states gave at the last
    /// muiComputeLayout that reached it, where its transitions have them.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param valuesOut  Receives the values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetVisualStyle(const muiContext* context,
                                                           muiNodeId nodeId,
                                                           muiVisualStyle* valuesOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_VISUAL_H
