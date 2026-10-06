// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scrolling (record mui-0007): a scroll container's offset (layout.h's
// scrollAxes) and the extent its children reach. Offsets are logical: x
// runs from the inline start, so under right to left it grows leftward.
// Painting moves the children by the offset through a transform, and
// hit testing and navigation follow them.

#ifndef MAUL_UI_SCROLL_H
#define MAUL_UI_SCROLL_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Scrolls a node to an offset at once, stopping any step easing it:
    /// within 0 and its extent less its padding box along each axis it
    /// scrolls (0 along any other), as its last muiComputeLayout measured
    /// them; layout keeps it within them as sizes change. The next draw
    /// list moves the children.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param x        The offset from the inline start, finite.
    /// @param y        The offset from the top, finite.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, an offset not finite or a call from a measure or
    ///         paint function; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetScroll(muiContext* context, muiNodeId nodeId,
                                                      float x, float y);

    /// Reads a node's scroll offset; 0 along an axis it does not scroll,
    /// as a node that stops scrolling along one drops its offset there.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param xOut     Receives the offset from the inline start.
    /// @param yOut     Receives the offset from the top.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetScroll(const muiContext* context, muiNodeId nodeId,
                                                      float* xOut, float* yOut);

    /// Reads the extent a scroll container's children reach, as its last
    /// muiComputeLayout measured it: from its padding box's start to the
    /// furthest end of its children's margin boxes plus its end padding,
    /// at least its padding box; for a scrollbar, the padding box over the
    /// extent is the thumb's share.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param extentOut  Receives the extent; 0 by 0 for a node that does
    ///                   not scroll or was not laid out since it does.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or
    ///         the null id; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetScrollExtent(const muiContext* context,
                                                            muiNodeId nodeId, muiSize* extentOut);

    /// Scrolls each scrolling ancestor of a node at once, the nearest
    /// first, stopping steps easing them: the least that brings the
    /// node's border box into its padding box, as CSSOM View's
    /// scrollIntoView with "nearest" does per axis. A node already inside
    /// stays; one past the start edge and no larger than the box aligns
    /// its start, one past the end its end; a larger one past either edge
    /// aligns the other, and one past both stays. Directional and
    /// sequential navigation does this to the node it focuses.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ScrollIntoView(muiContext* context, muiNodeId nodeId);

    // How input scrolls (muiWheelInput, muiKeyInput, muiNavigationInput).
    typedef struct muiScrollRule
    {
        // The distance a wheel detent scrolls, finite and at least 0.
        float wheelStep;
        // The distance an arrow scrolls, finite and at least 0.
        float lineStep;
        // The share of the scrollport a page key scrolls, above 0 and at
        // most 1.
        float pageFraction;
        // How long a scroll container keeps the wheel after its last
        // turn, in nanoseconds.
        uint64_t latchNs;
        // How long a whole detent's or a key's step eases out, in
        // nanoseconds; 0 jumps.
        uint64_t easeNs;
        // What a touch fling keeps of its speed each millisecond, above 0
        // and below 1.
        float decelerationRate;
        // Whether a touch pan past a limit moves the children on beyond
        // it, rubber banded as iOS does, springing back when released;
        // else it stops at the limit, as desktop browsers do.
        bool overscroll;
    } muiScrollRule;

    /// The default scroll rule: 100 a detent, Chrome's on Windows; 40 a
    /// line and 0.875 of the scrollport a page, Chrome's; a latch of
    /// 500 ms; steps easing out over 150 ms; flings keeping 0.998 of their
    /// speed a millisecond, iOS's normal rate; no overscroll. Hosts pass
    /// the platform's steps where it has them.
    ///
    /// @return The rule.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiScrollRule muiDefaultScrollRule(void);

    /// Sets the context's scroll rule.
    ///
    /// @param context  The context.
    /// @param rule     The rule, as described above.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         rule outside the above, or a call from a measure or paint
    ///         function.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetScrollRule(muiContext* context,
                                                     const muiScrollRule* rule);

    // A scrollbar thumb along its track.
    typedef struct muiScrollThumb
    {
        // From the track's start: its top, or its inline start.
        float start;
        float length;
    } muiScrollThumb;

    /// Places a scrollbar thumb for a node along an axis, as its last
    /// muiComputeLayout and its offset leave it: the track times the
    /// padding box over the extent, at least minimum (at most the track),
    /// and placed as the offset is between 0 and its limit. A node that
    /// cannot scroll that way fills the track.
    ///
    /// @param context     The context.
    /// @param nodeId      The node.
    /// @param horizontal  The axis: true for x.
    /// @param track       The track's length, finite and at least 0.
    /// @param minimum     The shortest thumb, finite and at least 0.
    /// @param thumbOut    Receives the thumb.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a length outside the above; `mui_errorStale` for
    ///         a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetScrollThumb(const muiContext* context,
                                                           muiNodeId nodeId, bool horizontal,
                                                           float track, float minimum,
                                                           muiScrollThumb* thumbOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_SCROLL_H
