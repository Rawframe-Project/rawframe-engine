// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transitions: specs of how a property moves to a new value (timed with
// an easing, or a spring), shared by the classes whose variants name
// them, and run against the time the host passes to muiComputeLayout
// (record mui-0004). The spec for a change is resolved through the same
// layers as values, from the state after the change. Numbers, insets,
// dimensions and radii that are Scale+Offset on both sides, colors (in
// premultiplied Oklab) and shadows move, ending exactly on their target;
// enumerators, flags, image keys, gradients, changes to or from
// automatic, direct writes, and every change under reduced motion apply
// at once.

#ifndef MAUL_UI_TRANSITION_H
#define MAUL_UI_TRANSITION_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/style.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        // The specs one variant of a class can name at once.
        MUI_MAX_VARIANT_TRANSITIONS = 4
    };

    // A transition spec, in the shape of every id (family record 0016).
    typedef struct muiTransitionId
    {
        uint32_t index1;
        uint32_t generation;
    } muiTransitionId;

    // How a spec moves a value.
    typedef uint8_t muiTransitionKind;

    enum
    {
        // Over a duration, along an easing curve.
        mui_transitionTimed = 0,
        // As a damped spring, keeping its speed when its target changes.
        mui_transitionSpring = 1,
    };

    // The easing curve of a timed transition, as CSS names them.
    typedef uint8_t muiEasing;

    enum
    {
        mui_easingLinear = 0,
        // cubic-bezier(0.25, 0.1, 0.25, 1).
        mui_easingEase = 1,
        // cubic-bezier(0.42, 0, 1, 1).
        mui_easingEaseIn = 2,
        // cubic-bezier(0, 0, 0.58, 1).
        mui_easingEaseOut = 3,
        // cubic-bezier(0.42, 0, 0.58, 1).
        mui_easingEaseInOut = 4,
        // The def's bezier control points.
        mui_easingCubicBezier = 5,
    };

    // A spec. Build it with muiDefaultTransitionDef.
    typedef struct muiTransitionDef
    {
        uint32_t cookie;
        muiTransitionKind kind;
        // Time from the change to the start of the motion.
        uint64_t delayNs;
        // For a timed transition: its length, its curve, and for
        // mui_easingCubicBezier the control points x1, y1, x2, y2, both x
        // from 0 to 1.
        uint64_t durationNs;
        muiEasing easing;
        float bezier[4];
        // For a spring: its natural frequency in hertz, above 0, and its
        // damping ratio, above 0 (1 is critical, below overshoots).
        float frequency;
        float dampingRatio;
    } muiTransitionDef;

    /// Returns the default transition def: timed, 250 ms, ease, no delay;
    /// as a spring, 2 Hz and critically damped.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiTransitionDef muiDefaultTransitionDef(void);

    /// Creates a transition spec.
    ///
    /// @param context          The context.
    /// @param def              The spec: a valid cookie, a known kind and
    ///                         easing, bezier x from 0 to 1 and finite y,
    ///                         and a finite frequency and damping ratio
    ///                         above 0.
    /// @param transitionIdOut  Receives the spec; set to the null id on
    ///                         failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def
    ///         outside the above or a call from a measure or paint function;
    ///         `mui_errorCapacity` when the context's transition limit is
    ///         reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateTransition(muiContext* context,
                                                        const muiTransitionDef* def,
                                                        muiTransitionId* transitionIdOut);

    /// Destroys a transition spec. Variants that name it name none, and
    /// transitions it started run to their end.
    ///
    /// @param context       The context.
    /// @param transitionId  The spec.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose spec is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyTransition(muiContext* context,
                                                         muiTransitionId transitionId);

    /// Names the spec one variant of a class gives properties; the null id
    /// takes the variant's spec away from them.
    ///
    /// @param context       The context.
    /// @param styleId       The class.
    /// @param variant       The variant.
    /// @param transitionId  The spec, or the null id.
    /// @param group         The properties' group.
    /// @param mask          The properties, within the group's.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null class id, an unknown variant, group or property bit or a
    ///         call from a measure or paint function; `mui_errorStale` for a class or a
    ///         spec that is gone; `mui_errorCapacity` when the variant
    ///         already names MUI_MAX_VARIANT_TRANSITIONS specs, or has no
    ///         values yet and the context's limit of property sets is
    ///         reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetTransition(muiContext* context, muiStyleId styleId,
                                                           muiVariant variant,
                                                           muiTransitionId transitionId,
                                                           muiPropertyGroup group,
                                                           muiPropertyMask mask);

    /// Reads the spec one variant of a class gives a property.
    ///
    /// @param context          The context.
    /// @param styleId          The class.
    /// @param variant          The variant.
    /// @param property         The property.
    /// @param transitionIdOut  Receives the spec; the null id for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property; `mui_errorStale`
    ///         for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetTransition(const muiContext* context,
                                                           muiStyleId styleId, muiVariant variant,
                                                           muiProperty property,
                                                           muiTransitionId* transitionIdOut);

    /// Returns whether a property of a node is moving.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param property  The property.
    /// @return Whether a transition of it runs; false for a stale id or a
    ///         NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API bool muiNode_IsTransitioning(const muiContext* context, muiNodeId nodeId,
                                         muiProperty property);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TRANSITION_H
