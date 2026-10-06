// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007): a node placed beside an anchor node after
// each layout. Its layer kind (maul-ui/interaction.h) says how it paints,
// usually mui_layerOverlay; this says where. From the anchor's border box
// on the surface, through scrolling, the popup's border box goes to the
// side asked, aligned along it, then on each axis flips to the opposite
// side when it overflows the root's box and the other side has more
// room, and is clamped into the root's box, its start edge kept when it
// cannot fit. Layout gives its size; an absolutely placed popup takes no
// room where it sits in the tree.
//
// A popup record stands for an open popup: the host sets it when it
// opens one and clears it, or destroys the node, when it closes. Light
// dismissal, as HTML's popovers have it, reports when one should close
// (mui_notificationPopupDismissed, the reason in its count); the host
// closes it, with whatever exit it likes. A popup anchored inside another
// nests under it. A pointer press dismisses every popup that neither
// holds the pressed node nor has its anchor holding it, nested ones
// first, and keeps the popups those nest under; an Escape no handler
// takes dismisses the popup set last; focus moved by code or navigation
// to a node outside a popup and its anchor dismisses it. Each is
// reported once, until the popup is set anew.

#ifndef MAUL_UI_POPUP_H
#define MAUL_UI_POPUP_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Where a popup goes from its anchor. Start and end follow the
    // anchor's direction: start is the left, or the right under right to
    // left.
    typedef uint8_t muiPopupSide;

    enum
    {
        mui_popupBelow = 0,
        mui_popupAbove = 1,
        mui_popupStart = 2,
        mui_popupEnd = 3,
        // On the anchor's center, as a dialog on the root's.
        mui_popupCenter = 4,
    };

    // How a popup lines up with its anchor along the side: their start
    // edges, centers, or end edges; for above and below, start and end in
    // the anchor's direction.
    typedef uint8_t muiPopupAlign;

    enum
    {
        mui_popupAlignStart = 0,
        mui_popupAlignCenter = 1,
        mui_popupAlignEnd = 2,
    };

    typedef struct muiPopup
    {
        // The node it is placed beside, in the same tree.
        muiNodeId anchor;
        muiPopupSide side;
        muiPopupAlign align;
        // Between the anchor and the popup, along the side; finite and at
        // least 0.
        float gap;
        // Kept clear inside the root's box; finite and at least 0.
        float margin;
        // Whether presses, Escape and focus dismiss it; false keeps it
        // open until the host closes it, as HTML's manual popovers.
        bool lightDismiss;
    } muiPopup;

    // Why a popup was dismissed.
    typedef uint8_t muiDismissReason;

    enum
    {
        // A pointer pressed outside it and its anchor.
        mui_dismissPress = 1,
        // An Escape no handler took.
        mui_dismissEscape = 2,
        // Focus moved outside it and its anchor.
        mui_dismissFocus = 3,
    };

    /// The default popup: below the null anchor, start edges aligned, no
    /// gap or margin, dismissed lightly.
    ///
    /// @return The popup.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiPopup muiDefaultPopup(void);

    /// Makes a node a popup, or sets its popup anew; placed at the next
    /// layout, and dismissible again.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param popup    The popup: a live anchor other than the node, a
    ///                 known side and alignment, gap and margin as above.
    /// @return `mui_success`; `mui_errorCapacity` when the context holds
    ///         its limit of popups; `mui_errorInvalid` for a NULL argument,
    ///         the null id, a popup outside the above, or a call from a
    ///         measure or paint function; `mui_errorStale` for a node or
    ///         anchor that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetPopup(muiContext* context, muiNodeId nodeId,
                                                     const muiPopup* popup);

    /// Reads a node's popup.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param popupOut  Receives the popup.
    /// @return `mui_success`; `mui_empty` for a node that is not a popup;
    ///         `mui_errorInvalid` for a NULL argument or the null id;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetPopup(const muiContext* context, muiNodeId nodeId,
                                                     muiPopup* popupOut);

    /// Reads the side a popup went to at the last layout that placed it,
    /// after flipping, so an arrow can point at the anchor.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param sideOut  Receives the side.
    /// @return `mui_success`; `mui_empty` for a node that is not a popup or
    ///         has not been placed since it was set; `mui_errorInvalid`
    ///         for a NULL argument or the null id; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetPopupSide(const muiContext* context,
                                                         muiNodeId nodeId, muiPopupSide* sideOut);

    /// Makes a node no longer a popup; it keeps its last place until
    /// layout places it again.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`, whether or not it was one;
    ///         `mui_errorInvalid` for a NULL context, the null id or a call
    ///         from a measure or paint function; `mui_errorStale` for a
    ///         node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_ClearPopup(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_POPUP_H
