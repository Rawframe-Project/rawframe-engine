// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The draw-command list (record mui-0005): what a renderer draws for a
// subtree, as fixed-size records in paint order, each with an index into
// a clip table and a transform table, so that a renderer evaluates clips
// per command and batches across them. Coordinates are logical units;
// colors are linear light with premultiplied alpha. Identical trees give
// byte-identical lists.

#ifndef MAUL_UI_DRAW_H
#define MAUL_UI_DRAW_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/visual.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A color in linear light, its red, green and blue premultiplied by
    // its alpha.
    typedef struct muiLinearColor
    {
        float r;
        float g;
        float b;
        float a;
    } muiLinearColor;

    // A value per corner, physical: top left first, then clockwise.
    typedef struct muiCorners
    {
        float topLeft;
        float topRight;
        float bottomRight;
        float bottomLeft;
    } muiCorners;

    // A value per side, physical: top first, then clockwise.
    typedef struct muiSides
    {
        float top;
        float right;
        float bottom;
        float left;
    } muiSides;

    // What a command draws.
    typedef uint32_t muiDrawKind;

    enum
    {
        // A box with rounded corners, its fill and its borders.
        mui_drawBox = 1,
        // A blurred shadow of a rounded box, outside or inside it.
        mui_drawShadow = 2,
        // An image the host names, stretched or in nine slices.
        mui_drawImage = 3,
        // Glyphs of one font, size and color, laid out by the host.
        mui_drawGlyphRun = 4,
    };

    // The color space a gradient's colors move through between stops.
    typedef uint32_t muiDrawInterpolation;

    enum
    {
        // Premultiplied Oklab, as transitions move colors.
        mui_interpolateOklab = 1,
    };

    enum
    {
        // The stops of a gradient in the list.
        MUI_MAX_DRAW_STOPS = 4
    };

    // A gradient of the gradient table. kind is mui_gradientLinear or
    // mui_gradientRadial (maul-ui/visual.h); angle is in degrees clockwise
    // from toward the top, for a linear one.
    typedef struct muiDrawGradient
    {
        uint32_t kind;
        uint32_t stopCount;
        muiDrawInterpolation interpolation;
        float angle;
        muiLinearColor colors[MUI_MAX_DRAW_STOPS];
        float positions[MUI_MAX_DRAW_STOPS];
    } muiDrawGradient;

    // A rounded box: rect is its border box. The gradient, an index into
    // the gradient table, 0 for none, is painted over fill; the borders lie
    // inside rect.
    typedef struct muiDrawBox
    {
        muiRect rect;
        muiCorners radii;
        muiLinearColor fill;
        uint32_t gradient;
        uint32_t reserved;
        muiSides borderWidths;
        // Top, right, bottom and left.
        muiLinearColor borderColors[4];
    } muiDrawBox;

    // A shadow of the rounded box rect and radii: outside it, or inside it
    // when inset is 1, offset, grown by spread and blurred over blur, as
    // CSS's box-shadow.
    typedef struct muiDrawShadow
    {
        muiRect rect;
        muiCorners radii;
        muiLinearColor color;
        float offsetX;
        float offsetY;
        float blur;
        float spread;
        uint32_t inset;
    } muiDrawShadow;

    // An image the host's key names, its uv rectangle (0 to 1 for all of
    // it) drawn into rect and multiplied by tint. Slice insets, in image
    // pixels, cut it into nine parts whose corners keep their size at one
    // logical unit per pixel; all 0 stretches it whole.
    typedef struct muiDrawImage
    {
        muiRect rect;
        uint64_t image;
        muiRect uv;
        muiSides slice;
        muiLinearColor tint;
    } muiDrawImage;

    // A run of glyphs of the glyph table: firstGlyph and glyphCount name
    // its span. font is the key the host's text gives out, size its em in
    // logical units, and each glyph is placed relative to the origin, a
    // point on the baseline.
    typedef struct muiDrawGlyphRun
    {
        uint64_t font;
        float originX;
        float originY;
        float size;
        uint32_t firstGlyph;
        uint32_t glyphCount;
        uint32_t reserved;
        muiLinearColor color;
    } muiDrawGlyphRun;

    // A glyph of a run: its id in the run's font, and its position from the
    // run's origin, in logical units, y down.
    typedef struct muiGlyph
    {
        uint32_t id;
        float x;
        float y;
    } muiGlyph;

    // A command: what it draws, the clip it is drawn in (an index of the
    // clip table, 0 for none) and the transform its coordinates go through
    // (an index of the transform table).
    typedef struct muiDrawCommand
    {
        muiDrawKind kind;
        uint32_t clip;
        uint32_t transform;
        uint32_t reserved;
        union
        {
            muiDrawBox box;
            muiDrawShadow shadow;
            muiDrawImage image;
            muiDrawGlyphRun glyphRun;
        };
    } muiDrawCommand;

    // A clip: drawing is kept inside the rounded rect, or outside it when
    // invert is 1, and inside its parent, an index of the clip table (0
    // for none), too.
    typedef struct muiDrawClip
    {
        muiRect rect;
        muiCorners radii;
        uint32_t parent;
        uint32_t transform;
        uint32_t invert;
    } muiDrawClip;

    // A 2D affine transform: x' = a x + c y + e, y' = b x + d y + f.
    typedef struct muiDrawTransform
    {
        float a;
        float b;
        float c;
        float d;
        float e;
        float f;
    } muiDrawTransform;

    // What a list is for.
    typedef struct muiDrawHeader
    {
        // The host's key for the surface, as muiDrawInput gave it.
        uint64_t surface;
        // Counts the context's builds, from 1.
        uint64_t generation;
        // The root's size, in logical units.
        float width;
        float height;
        // Device pixels per logical unit.
        float scale;
        uint32_t reserved;
    } muiDrawHeader;

    // A list, valid until the context's next build. Index 0 of the clip
    // and gradient tables is a placeholder for none; entry 0 of the
    // transform table is the identity. Glyph runs take spans of the glyph
    // table.
    typedef struct muiDrawList
    {
        muiDrawHeader header;
        const muiDrawCommand* commands;
        uint32_t commandCount;
        uint32_t clipCount;
        const muiDrawClip* clips;
        const muiDrawTransform* transforms;
        uint32_t transformCount;
        uint32_t gradientCount;
        const muiDrawGradient* gradients;
        const muiGlyph* glyphs;
        uint32_t glyphCount;
        uint32_t reserved;
    } muiDrawList;

    // Where a paint function adds what it draws, valid during the call.
    typedef struct muiDrawSink muiDrawSink;

    // A run of glyphs a paint function adds: the font key, its size in
    // logical units, above 0, its color, sRGB-encoded with straight alpha
    // as the text style gives it, and its origin, a point on the baseline
    // relative to the content box's top left.
    typedef struct muiGlyphRun
    {
        uint64_t font;
        float size;
        muiColor color;
        float originX;
        float originY;
    } muiGlyphRun;

    // Paints a node's host content into sink: called during
    // muiBuildDrawList for each node whose content is the host's, with its
    // host key and its content box's size. The context refuses edits made
    // from it; reads, such as muiNode_GetTextStyle, are allowed.
    typedef void (*muiPaintFunction)(void* user, muiNodeId nodeId, uint64_t hostKey, float width,
                                     float height, muiDrawSink* sink);

    // What a build draws for.
    typedef struct muiDrawInput
    {
        uint64_t surface;
        // Device pixels per logical unit, above 0: what snapping rounds to.
        float scale;
        // Paints host content; NULL paints none.
        muiPaintFunction paint;
        void* paintUser;
    } muiDrawInput;

    /// Adds a run of glyphs to the node being painted, after what it added
    /// before, in the clip its children are drawn in. Its color is
    /// converted to linear light and multiplied by the node's opacity; at
    /// the identity transform its baseline snaps to a device pixel.
    ///
    /// @param sink        The sink the paint function was given.
    /// @param run         The run.
    /// @param glyphs      Its glyphs, at finite positions.
    /// @param glyphCount  How many, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, no
    ///         glyphs, a size that is not a finite number above 0, a color
    ///         outside 0 to 1, or an origin or a position that is not
    ///         finite; `mui_errorCapacity` when the list needs more
    ///         commands or glyphs than the context's limits, which fails
    ///         the build.
    /// @par Thread safety
    /// Safe from any thread; the sink is used by one thread at a time, and
    /// only during the call of the paint function given it.
    MUI_NODISCARD MUI_API muiResult muiDrawSink_AddGlyphRun(muiDrawSink* sink,
                                                            const muiGlyphRun* run,
                                                            const muiGlyph* glyphs,
                                                            uint32_t glyphCount);

    /// Paints a root's subtree, as its last muiComputeLayout left it, into
    /// the context's list, and clears the subtree's paint requests. When
    /// nothing below the root asked for paint since the last build of the
    /// same root, surface and scale, the list stays as it is, generation
    /// and all; otherwise subtrees nothing asked to repaint, at the origin
    /// and opacity they were painted at, copy their commands from the last
    /// list, which gives the bytes a build from nothing would. Per node, in paint order: its
    /// outer shadow, its box, its inner shadow, its image and what the paint function adds for
    /// host content, then its children, depth first; a node that clips draws its host content
    /// and its children inside its rounded border box. Opacity multiplies down
    /// the subtree into every command's colors. At the identity transform, box and image edges and
    /// clips snap to device pixels, and border widths to whole device pixels, at least one.
    ///
    /// @param context  The context.
    /// @param rootId   The root.
    /// @param input    The surface, the scale and the paint function.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a scale that is not a finite number above 0, or a
    ///         call from a measure or paint function; `mui_errorStale` for a root that
    ///         is gone; `mui_errorCapacity` when the list needs more commands,
    ///         clips, gradients or glyphs than the context's limits, which
    ///         leaves the list empty.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiBuildDrawList(muiContext* context, muiNodeId rootId,
                                                     const muiDrawInput* input);

    /// Shows the context's last list.
    ///
    /// @param context  The context.
    /// @param listOut  Receives the list; empty before any build.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGetDrawList(const muiContext* context, muiDrawList* listOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_DRAW_H
