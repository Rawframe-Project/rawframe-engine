// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout: the authored values that size and place a node, the solver
// that computes rectangles from them (CSS Flexbox, record mui-0003), and
// the rectangles it publishes. Lengths are logical units; a node's
// rectangle is relative to its parent's border box, and a root's
// rectangle starts at 0, 0.

#ifndef MAUL_UI_LAYOUT_H
#define MAUL_UI_LAYOUT_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Whether a dimension is automatic or a Scale+Offset value.
    typedef uint8_t muiDimensionKind;

    enum
    {
        // Sized by the layout rules: content, stretching, flexing. As a
        // maximum, no limit; as a minimum, the automatic minimum size.
        mui_dimensionAuto = 0,
        // scale x the parent's content extent on the axis + offset; with an
        // indefinite parent extent, automatic. As in CSS, a parent's height
        // is indefinite when it is its content's: not given, not stretched
        // across a line, not flexed in a column of definite height. A
        // scaled cross size is not automatic, so it does not stretch.
        mui_dimensionValue = 1,
    };

    // A size along one axis. A zeroed dimension is automatic.
    typedef struct muiDimension
    {
        float scale;
        float offset;
        muiDimensionKind kind;
    } muiDimension;

    // A node's size, minimum and maximum, per axis. The size is the
    // border box.
    typedef struct muiSizing
    {
        muiDimension width;
        muiDimension height;
        muiDimension minWidth;
        muiDimension minHeight;
        muiDimension maxWidth;
        muiDimension maxHeight;
        // The preferred width divided by height of the border box, 0 for
        // none: a definite size on one axis gives the other.
        float aspectRatio;
    } muiSizing;

    // The four sides of a box, in logical order: start and end follow the
    // inline direction.
    typedef struct muiEdges
    {
        float start;
        float end;
        float top;
        float bottom;
    } muiEdges;

    // The main axis of a container and the direction its children follow.
    typedef uint8_t muiFlexDirection;

    enum
    {
        mui_flexRow = 0,
        mui_flexRowReverse = 1,
        mui_flexColumn = 2,
        mui_flexColumnReverse = 3,
    };

    // How a container places its children along the main axis.
    typedef uint8_t muiJustify;

    enum
    {
        mui_justifyStart = 0,
        mui_justifyEnd = 1,
        mui_justifyCenter = 2,
        mui_justifySpaceBetween = 3,
        mui_justifySpaceAround = 4,
        mui_justifySpaceEvenly = 5,
    };

    // How children sit on the cross axis.
    typedef uint8_t muiAlign;

    enum
    {
        // For a child: the container's alignment. Not valid for a
        // container.
        mui_alignAuto = 0,
        mui_alignStretch = 1,
        mui_alignStart = 2,
        mui_alignEnd = 3,
        mui_alignCenter = 4,
        // First baselines line up, as CSS's baseline: in a row container
        // the children so aligned share their line's baseline; in a column
        // container it is start.
        mui_alignBaseline = 5,
    };

    // Whether a container breaks its children into lines.
    typedef uint8_t muiFlexWrap;

    enum
    {
        // One line, which the children shrink or overflow to fit.
        mui_wrapNone = 0,
        // Lines follow each other from the cross start.
        mui_wrapWrap = 1,
        // Lines follow each other from the cross end.
        mui_wrapReverse = 2,
    };

    // How a container with several lines places them on the cross axis.
    typedef uint8_t muiAlignContent;

    enum
    {
        // The lines share the free space equally, growing.
        mui_alignContentStretch = 0,
        mui_alignContentStart = 1,
        mui_alignContentEnd = 2,
        mui_alignContentCenter = 3,
        mui_alignContentSpaceBetween = 4,
        mui_alignContentSpaceAround = 5,
        mui_alignContentSpaceEvenly = 6,
    };

    // What a node lays out as a container.
    typedef struct muiFlexContainer
    {
        muiFlexDirection direction;
        muiFlexWrap wrap;
        muiJustify justify;
        muiAlign alignItems;
        muiAlignContent alignContent;
        // The space between rows, and between columns, of children.
        float rowGap;
        float columnGap;
    } muiFlexContainer;

    // How a node takes part in its parent's flex layout.
    typedef struct muiFlexItem
    {
        float grow;
        float shrink;
        muiDimension basis;
        muiAlign alignSelf;
    } muiFlexItem;

    // A value per side, physical: top first, then clockwise.
    typedef struct muiSides
    {
        float top;
        float right;
        float bottom;
        float left;
    } muiSides;

    // Sides of a box, as bits.
    typedef uint8_t muiEdgeMask;

    enum
    {
        mui_edgeStart = 1,
        mui_edgeEnd = 2,
        mui_edgeTop = 4,
        mui_edgeBottom = 8,
    };

    // Whether a node is a flex item of its parent or placed by insets.
    typedef uint8_t muiPositionKind;

    enum
    {
        mui_positionFlow = 0,
        // Out of the parent's flex layout, placed by insets in the parent's
        // padding box, against which its Scale+Offset values resolve.
        mui_positionAbsolute = 1,
    };

    // Distances from the sides of the parent's padding box.
    typedef struct muiInsets
    {
        muiDimension start;
        muiDimension end;
        muiDimension top;
        muiDimension bottom;
    } muiInsets;

    // Where an absolute node goes.
    typedef struct muiPlacement
    {
        muiPositionKind position;
        // An automatic inset leaves that side free; with both sides of an
        // axis free, the node sits where it would as its parent's only
        // child.
        muiInsets inset;
        // Moves the node back by this fraction of its size, from 0 to 1 per
        // axis, after the insets place it: with start and top insets, the
        // point of the node they place, so 0.5, 0.5 centers it on them. The
        // x fraction is measured from the inline start. In-flow nodes
        // ignore it.
        float anchorX;
        float anchorY;
    } muiPlacement;

    // The inline direction of a node and the nodes below it that inherit.
    typedef uint8_t muiTextDirection;

    enum
    {
        // The parent's direction; left to right for a root.
        mui_textInherit = 0,
        mui_textLeftToRight = 1,
        // Start is on the right: rows run right to left and every start and
        // end edge, inset and anchor mirrors.
        mui_textRightToLeft = 2,
    };

    // What a node without children holds.
    typedef uint8_t muiContentKind;

    enum
    {
        // Nothing: its content box is empty.
        mui_contentNone = 0,
        // The host's content, sized by the measure function given to
        // muiComputeLayout. A node with children ignores it.
        mui_contentHost = 1,
    };

    // The axes a node scrolls its children along. A node that scrolls in
    // either is a scroll container: it clips its children at its rounded
    // padding box, its automatic minimum size is 0, as CSS's, and layout
    // measures the extent its children reach (maul-ui/scroll.h).
    typedef uint8_t muiScrollAxes;

    enum
    {
        mui_scrollNone = 0,
        mui_scrollHorizontal = 1,
        mui_scrollVertical = 2,
        mui_scrollBoth = 3,
    };

    // Every authored value layout reads. Build it with
    // muiDefaultLayoutStyle.
    typedef struct muiLayoutStyle
    {
        muiSizing sizing;
        muiFlexContainer container;
        muiFlexItem item;
        muiEdges margin;
        // The margins that are automatic, taking free space; their lengths
        // in margin are unused.
        muiEdgeMask marginAuto;
        muiEdges border;
        muiEdges padding;
        muiPlacement placement;
        muiTextDirection textDirection;
        muiContentKind content;
        muiScrollAxes scrollAxes;
        // The edges whose padding is at least the surface's safe-area
        // inset on the physical side each falls on in the node's direction
        // (muiLayoutInput): where content meets the surface's edge, as a
        // bar whose background reaches it and whose content does not.
        // Nothing is consumed: a node below one that asks pads again.
        muiEdgeMask safeArea;
    } muiLayoutStyle;

    // A rectangle: its origin and size.
    typedef struct muiRect
    {
        float x;
        float y;
        float width;
        float height;
    } muiRect;

    // A width and a height.
    typedef struct muiSize
    {
        float width;
        float height;
    } muiSize;

    // What the solver asks of host content along one axis.
    typedef uint8_t muiMeasureMode;

    enum
    {
        // The content box is exactly size.
        mui_measureExact = 0,
        // The content fits within size where it can, as text wraps to a
        // width.
        mui_measureAtMost = 1,
        // The content at its widest: size is unused.
        mui_measureMaxContent = 2,
        // The content at its narrowest, as text broken at every
        // opportunity: size is unused.
        mui_measureMinContent = 3,
    };

    // One axis of a measurement request.
    typedef struct muiMeasureAxis
    {
        float size;
        muiMeasureMode mode;
    } muiMeasureAxis;

    // Returns the content-box size of a node's host content. It runs inside
    // muiComputeLayout, on the calling thread, and may not change the
    // context; a call that would is refused as misuse. It is never asked
    // with both axes exact, as the size is then decided: a host lays its
    // content out for painting at the node's rectangle. Its answer is to
    // depend on the request alone, and content that fits within a size is
    // to measure the same within any smaller one it still fits, as text
    // broken greedily into lines does: layout keeps answers and gives them
    // again to such requests.
    typedef muiSize (*muiMeasureFunction)(void* user, muiNodeId nodeId, uint64_t hostKey,
                                          muiMeasureAxis width, muiMeasureAxis height);

    // Returns the first baseline of a node's host content laid out at a
    // content-box size: its distance down from the content box's top, or
    // NaN when the content has none. It runs as the measure function does,
    // with the same user pointer.
    typedef float (*muiBaselineFunction)(void* user, muiNodeId nodeId, uint64_t hostKey,
                                         float width, float height);

    // What muiComputeLayout lays a root out in.
    typedef struct muiLayoutInput
    {
        // The space the root fits into, at least 0.
        float availableWidth;
        float availableHeight;
        // Sizes host content; NULL sizes it as empty.
        muiMeasureFunction measure;
        void* measureUser;
        // Now, in nanoseconds on a monotonic clock, as Maul Window stamps
        // events: running transitions move to it. A time before the last
        // counts as no time passed.
        uint64_t timeNs;
        // Gives host content's baseline, for baseline alignment, with
        // measureUser; NULL gives none, and a baseline is then the bottom
        // of the node's border box, as CSS synthesizes one.
        muiBaselineFunction baseline;
        // The surface's safe-area insets, physical, finite and 0 or more:
        // what a display's cutouts, rounded corners and system bars cover,
        // as iOS's safeAreaInsets and Android's WindowInsets give them.
        // Nodes pad by them on the edges their safeArea names; new ones
        // lay those nodes out again.
        muiSides safeArea;
    } muiLayoutInput;

    /// Returns the default layout style: CSS's initial values (row, one
    /// line, no grow, shrink 1, automatic basis and sizes, stretched items
    /// and lines, start), no
    /// margins, borders or padding, and no content.
    ///
    /// @return The style.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiLayoutStyle muiDefaultLayoutStyle(void);

    /// Writes every layout property of a node directly, so that they win
    /// over its style classes until reset (muiNode_SetLayoutValues in
    /// maul-ui/style.h writes some). The node and its parent are laid out
    /// again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param style    The values: finite numbers, grow and shrink, padding,
    ///                 border and gaps at least 0, known enumerators and
    ///                 edge bits, alignItems not mui_alignAuto, and anchors
    ///                 from 0 to 1, and an aspect ratio of 0 or more.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a value outside the above, or a call from a measure
    ///         function; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutStyle(muiContext* context, muiNodeId nodeId,
                                                           const muiLayoutStyle* style);

    /// Reads a node's resolved layout values: its direct writes, and for
    /// the other properties what its classes and states gave at the last
    /// muiComputeLayout that reached it.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param styleOut  Receives the values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetLayoutStyle(const muiContext* context,
                                                           muiNodeId nodeId,
                                                           muiLayoutStyle* styleOut);

    /// Whether a node's content runs right to left: the direction its own
    /// resolved layout values give, else the nearest ancestor's that gives
    /// one, else left to right. It reads the values as they are at the
    /// call, so a measure function sees the direction layout uses.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return true for right to left; false for left to right, a NULL
    ///         context, the null id or a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API bool muiNode_IsRightToLeft(const muiContext* context, muiNodeId nodeId);

    /// Tells the solver a node's host content changed size, so it is
    /// measured again at the next muiComputeLayout.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the null
    ///         id or a call from a measure or paint function; `mui_errorStale` for an
    ///         id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_MarkContentChanged(muiContext* context,
                                                               muiNodeId nodeId);

    /// Lays out a root and its subtree in the given space. Subtrees that
    /// did not change since the last call are not visited.
    ///
    /// @param context  The context.
    /// @param rootId   A root: a node without a parent.
    /// @param input    The space and the measure function.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a node with a parent, a negative or non-finite
    ///         space, or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiComputeLayout(muiContext* context, muiNodeId rootId,
                                                     const muiLayoutInput* input);

    /// Returns whether muiComputeLayout on a root has work to do: an edit
    /// below it since its last run, a node whose conditions read a size or
    /// direction that run changed, which a following run styles again, a
    /// transition running below it, or a scroll step easing below it
    /// (maul-ui/scroll.h).
    ///
    /// @param context  The context.
    /// @param rootId   The root.
    /// @return Whether work is pending; false for a stale id or a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API bool muiIsUpdatePending(const muiContext* context, muiNodeId rootId);

    /// Returns a node's border box from the last muiComputeLayout that
    /// reached it, relative to its parent's border box.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The rectangle; all zero before any layout, for a stale id or
    ///         a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId);

    /// Returns a node's content box from the last muiComputeLayout that
    /// reached it, relative to its border box: inside its border and
    /// padding, the start's on the right in a right-to-left node, as its
    /// paint function and its text's carets are given it.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The rectangle; all zero before any layout, for a stale id or
    ///         a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiRect muiNode_GetContentRect(const muiContext* context, muiNodeId nodeId);

    /// Carries a point of a node's border box into the space its topmost
    /// ancestor's rectangle is in, where pointer events are given, through
    /// its own and its ancestors' places, local scales and scroll
    /// containers' offsets as the last muiComputeLayout, styling and
    /// scrolling left them: so a host places a
    /// window's candidate box at a caret, or its own popup beside a node.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param x        The point, from the border box's top left.
    /// @param y        Likewise.
    /// @param xOut     Receives the point's x there; unchanged on failure.
    /// @param yOut     Likewise its y.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         point not finite; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_MapToRoot(const muiContext* context, muiNodeId nodeId,
                                                      float x, float y, float* xOut, float* yOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_LAYOUT_H
