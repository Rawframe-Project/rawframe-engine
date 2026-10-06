// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interaction properties and hit testing (record mui-0007): whether a node
// and its children are hit by a point, whether input it leaves unused
// passes through to what lies behind the UI, whether it roots a layer,
// and which node is topmost at a point, in reverse paint order.

#ifndef MAUL_UI_INTERACTION_H
#define MAUL_UI_INTERACTION_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Which of a node and its children a point can hit.
    typedef uint8_t muiHitMode;

    enum
    {
        // The node, in its rounded border box, and its children: CSS's
        // pointer-events auto.
        mui_hitAuto = 0,
        // Its children alone: a container that lets points through to
        // what lies under it where it has no children.
        mui_hitChildren = 1,
        // Neither: the node and its subtree are passed over.
        mui_hitNone = 2,
    };

    // Whether a node roots a layer: painted, and hit, apart from the
    // content around it, above it.
    typedef uint8_t muiLayerKind;

    enum
    {
        // Part of its parent's layer.
        mui_layerNone = 0,
        // An activation layer, such as a dialog or a menu: painted after
        // the content it is in, above the layers activated before it.
        mui_layerActivation = 1,
        // An activation layer that is modal: points that miss it reach
        // nothing below it.
        mui_layerModal = 2,
        // In the overlay band, above every activation layer: popups and
        // tooltips.
        mui_layerOverlay = 3,
    };

    // Where an exiting node (maul-ui/exit.h) stays in its parent's layout.
    typedef uint8_t muiExitLayout;

    enum
    {
        // In its place until it is destroyed: its siblings move then.
        mui_exitKeep = 0,
        // Out of its parent's flow at once, at its last rectangle, so its
        // siblings close up while it plays out (Motion's popLayout).
        mui_exitPop = 1,
    };

    // Whether a node takes a player's focus, and how.
    typedef uint8_t muiFocusMode;

    enum
    {
        // Never.
        mui_focusNone = 0,
        // By a pointer press and by code, but sequential navigation passes
        // it over: the web's tabindex -1.
        mui_focusPointer = 1,
        // Also by sequential navigation.
        mui_focusAll = 2,
    };

    // A node's interaction values. Every field is a property
    // (mui_propertyHitMode, mui_propertyPassThrough, mui_propertyLayer,
    // mui_propertyFocusMode, mui_propertyTabOrder, mui_propertyDrags,
    // mui_propertyAccepts, mui_propertyExitLayout),
    // set like any other through classes, states and direct writes, and
    // not inherited.
    typedef struct muiInteractionStyle
    {
        muiHitMode hitMode;
        // Whether input the node is hit by but leaves unused passes to
        // what lies behind the UI, such as a game world: a HUD panel
        // that does not block clicks.
        bool passThrough;
        // A node that roots a layer is painted after the layer it is in
        // (the base, or another layer), at its laid-out place but outside
        // its ancestors' clips and opacity, as the web's top layer is.
        // Activation layers come in the order they became layers or were
        // raised, the overlay band after them all. Up to the context's
        // layers limit: a node that becomes a layer past it stays in its
        // parent's layer until its kind changes again.
        muiLayerKind layer;
        // Disabled and exiting nodes take no focus whatever their mode.
        muiFocusMode focusMode;
        // Where sequential navigation reaches the node: 0 in tree order,
        // after every node of 1 to 255, which come first, ascending, ties
        // in tree order.
        uint8_t tabOrder;
        // Whether a press on the node, or below it where no nearer node
        // takes drags, becomes a drag once it moves past the drag
        // threshold (maul-ui/pointer.h).
        bool drags;
        // The kinds of thing dropped on the node it takes: a drag offering
        // a kind in this mask (muiPointer_Offer) over the node, or over a
        // node below it that takes none of that kind, may drop here. The
        // bits are the application's; 0 takes nothing.
        uint32_t accepts;
        // Where the node stays in layout while it exits, read when its
        // exit begins.
        muiExitLayout exitLayout;
    } muiInteractionStyle;

    /// Returns the default interaction values: hit in full, blocking, no
    /// layer, taking no focus.
    ///
    /// @return The values.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiInteractionStyle muiDefaultInteractionStyle(void);

    /// Sets interaction values of one variant of a class, as
    /// muiStyle_SetLayoutValues does layout ones.
    ///
    /// @param context  The context.
    /// @param styleId  The class.
    /// @param variant  The variant.
    /// @param values   The values; only the fields mask names are read: a
    ///                 known hit mode, layer kind and focus mode.
    /// @param mask     The properties, within MUI_INTERACTION_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property bit, a value outside
    ///         the above or a call from a measure or paint function, which
    ///         changes nothing; `mui_errorStale` for an id whose class is
    ///         gone; `mui_errorCapacity` when the variant had no values and
    ///         the context's limit of property sets is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetInteractionValues(muiContext* context,
                                                                  muiStyleId styleId,
                                                                  muiVariant variant,
                                                                  const muiInteractionStyle* values,
                                                                  muiPropertyMask mask);

    /// Reads the interaction values one variant of a class sets.
    ///
    /// @param context    The context.
    /// @param styleId    The class.
    /// @param variant    The variant.
    /// @param valuesOut  Receives the set values, and
    ///                   muiDefaultInteractionStyle's for the rest.
    /// @param maskOut    Receives which interaction properties are set.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or an unknown variant; `mui_errorStale` for an id
    ///         whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetInteractionValues(const muiContext* context,
                                                                  muiStyleId styleId,
                                                                  muiVariant variant,
                                                                  muiInteractionStyle* valuesOut,
                                                                  muiPropertyMask* maskOut);

    /// Writes interaction properties of a node directly, as
    /// muiNode_SetLayoutValues does layout ones; neither its layout nor
    /// its paint is redone.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param values   The values, as muiStyle_SetInteractionValues takes
    ///                 them.
    /// @param mask     The properties, within MUI_INTERACTION_PROPERTIES.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown property bit, a value outside the above
    ///         or a call from a measure or paint function, which changes
    ///         nothing; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetInteractionValues(muiContext* context,
                                                                 muiNodeId nodeId,
                                                                 const muiInteractionStyle* values,
                                                                 muiPropertyMask mask);

    /// Reads a node's resolved interaction values: its direct writes, and
    /// for the other properties what its classes and states gave at the
    /// last muiComputeLayout that reached it.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param valuesOut  Receives the values.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetInteractionStyle(const muiContext* context,
                                                                muiNodeId nodeId,
                                                                muiInteractionStyle* valuesOut);

    // What a point hits: the node, the null id for none, the point in its
    // border box, and whether input it leaves unused passes through to
    // what lies behind the UI (always, when nothing is hit; never, when a
    // modal layer blocks it).
    typedef struct muiHit
    {
        muiNodeId node;
        float x;
        float y;
        bool passThrough;
    } muiHit;

    /// Finds the topmost node of a root's subtree at a point, as its last
    /// muiComputeLayout left it: the last in paint order whose rounded
    /// border box holds the point, inside the rounded clips of every
    /// ancestor that clips and of no node whose hit mode leaves it out.
    /// Layers are tried from the top down, then the content they are
    /// not in; a point a modal layer's subtree misses hits the modal
    /// layer's root, blocked, and nothing below it. Opacity does not
    /// matter, as in CSS. Positions are those painting gives, the root at
    /// its own rectangle.
    ///
    /// @param context  The context.
    /// @param rootId   The root of the subtree.
    /// @param x        The point, in the space the root's rectangle is in.
    /// @param y        Likewise.
    /// @param hitOut   Receives what the point hits; unchanged on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id or a point not finite; `mui_errorStale` for a root
    ///         that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiHitTest(const muiContext* context, muiNodeId rootId, float x,
                                               float y, muiHit* hitOut);

    /// Raises a layer above the others of its band, as activating a
    /// window brings it to the front: it becomes the latest activated.
    ///
    /// @param context  The context.
    /// @param nodeId   A node that roots a layer, as its last style
    ///                 resolution or direct write left it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id, a node that roots no layer or a call from a
    ///         measure or paint function; `mui_errorStale` for a node that
    ///         is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_RaiseLayer(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_INTERACTION_H
