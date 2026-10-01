// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over transition specs and the variants that name
// them (record mui-0004).

#include "maul-ui/transition.h"

#include "animation.h"
#include "context.h"
#include "easing.h"
#include "pool.h"
#include "property.h"
#include "style_edit.h"
#include "style_store.h"

#include <math.h>

#define TRANSITION_DEF_COOKIE 0x6D757472u // "mutr"

muiTransitionDef muiDefaultTransitionDef(void)
{
    return (muiTransitionDef){
        .cookie = TRANSITION_DEF_COOKIE,
        .kind = mui_transitionTimed,
        .durationNs = 250000000u,
        .easing = mui_easingEase,
        .bezier = {0.25f, 0.1f, 0.25f, 1.0f},
        .frequency = 2.0f,
        .dampingRatio = 1.0f,
    };
}

static bool IsUnit(float value)
{
    return value >= 0.0f && value <= 1.0f;
}

static bool IsDefValid(const muiTransitionDef* def)
{
    return def->cookie == TRANSITION_DEF_COOKIE && def->kind <= mui_transitionSpring &&
           def->easing <= mui_easingCubicBezier && IsUnit(def->bezier[0]) &&
           isfinite(def->bezier[1]) && IsUnit(def->bezier[2]) && isfinite(def->bezier[3]) &&
           isfinite(def->frequency) && def->frequency > 0.0f && isfinite(def->dampingRatio) &&
           def->dampingRatio > 0.0f;
}

// The curve of an easing, from CSS Easing Functions' keywords.
static muiCurve CurveOf(const muiTransitionDef* def)
{
    static const double keywords[][4] = {
        {0.0, 0.0, 1.0, 1.0},  {0.25, 0.1, 0.25, 1.0}, {0.42, 0.0, 1.0, 1.0},
        {0.0, 0.0, 0.58, 1.0}, {0.42, 0.0, 0.58, 1.0},
    };
    if (def->easing == mui_easingCubicBezier)
    {
        return muiMakeCurve((double)def->bezier[0], (double)def->bezier[1], (double)def->bezier[2],
                            (double)def->bezier[3]);
    }
    const double* p = keywords[def->easing];
    return muiMakeCurve(p[0], p[1], p[2], p[3]);
}

muiResult muiCreateTransition(muiContext* context, const muiTransitionDef* def,
                              muiTransitionId* transitionIdOut)
{
    if (transitionIdOut != nullptr)
    {
        *transitionIdOut = (muiTransitionId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || transitionIdOut == nullptr || !IsDefValid(def) ||
        muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiAnimationStore* store = &context->animations;
    uint32_t slot = muiPoolTake(&store->specPool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    store->specs[slot - 1] = (muiTransitionSpec){.def = *def, .curve = CurveOf(def)};
    *transitionIdOut = (muiTransitionId){slot, muiPoolGeneration(&store->specPool, slot)};
    return mui_success;
}

muiResult muiDestroyTransition(muiContext* context, muiTransitionId transitionId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (transitionId.index1 == 0 || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiAnimationStore* store = &context->animations;
    uint32_t slot = muiPoolResolve(&store->specPool, transitionId.index1, transitionId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    // Running transitions hold a copy, and variants that name the spec
    // find it gone.
    muiPoolGive(&store->specPool, slot);
    return mui_success;
}

// Takes properties away from every binding of a set, dropping empty
// ones.
static void Unbind(muiPropertySet* set, muiPropertyBits properties)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < set->bindingCount; i++)
    {
        muiTransitionBinding binding = set->bindings[i];
        binding.properties = muiWithout(binding.properties, properties);
        if (muiAnyProperty(binding.properties))
        {
            set->bindings[kept++] = binding;
        }
    }
    set->bindingCount = kept;
}

// Gives a spec to properties of a set: added to its binding of that spec,
// or a new binding. False when the set names its most specs already.
static bool Bind(muiPropertySet* set, muiTransitionId transition, muiPropertyBits properties)
{
    Unbind(set, properties);
    for (uint32_t i = 0; i < set->bindingCount; i++)
    {
        muiTransitionId named = set->bindings[i].transition;
        if (named.index1 == transition.index1 && named.generation == transition.generation)
        {
            set->bindings[i].properties = muiUnion(set->bindings[i].properties, properties);
            return true;
        }
    }
    if (set->bindingCount == MUI_MAX_VARIANT_TRANSITIONS)
    {
        return false;
    }
    set->bindings[set->bindingCount++] = (muiTransitionBinding){properties, transition};
    return true;
}

// Unbinding comes first so that a full variant can still take a spec
// that one of its bindings gives up entirely.
static muiResult SetBinding(muiContext* context, muiStyleClass* class, muiVariant variant,
                            muiTransitionId transition, muiPropertyBits properties)
{
    muiStyleStore* store = &context->style;
    if (transition.index1 == 0)
    {
        if (class->sets[variant] != 0)
        {
            Unbind(&store->sets[class->sets[variant] - 1], properties);
            muiReleaseEmptySet(store, class, variant);
        }
        return mui_success;
    }
    muiPropertySet* set = muiTakeVariantSet(store, class, variant);
    if (set == nullptr)
    {
        return mui_errorCapacity;
    }
    // A set that is full of bindings existed already, so none is wasted.
    if (!Bind(set, transition, properties))
    {
        return mui_errorCapacity;
    }
    return mui_success;
}

muiResult muiStyle_SetTransition(muiContext* context, muiStyleId styleId, muiVariant variant,
                                 muiTransitionId transitionId, muiPropertyGroup group,
                                 muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsGroupMaskKnown(group, mask))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleClass* class = &context->style.classes[slot - 1];
    if (!muiHasVariant(class, variant))
    {
        return muiRefuse(context);
    }
    if (transitionId.index1 != 0 && muiFindSpec(&context->animations, transitionId) == nullptr)
    {
        return mui_errorStale;
    }
    status = SetBinding(context, class, variant, transitionId, muiPropertiesOf(group, mask));
    if (status == mui_success)
    {
        muiRestyleAll(context);
    }
    return status;
}

muiResult muiStyle_GetTransition(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                 muiProperty property, muiTransitionId* transitionIdOut)
{
    if (context == nullptr || transitionIdOut == nullptr || styleId.index1 == 0 ||
        !muiIsPropertyKnown(property))
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiStyleClass* class = &store->classes[slot - 1];
    if (!muiHasVariant(class, variant))
    {
        return mui_errorInvalid;
    }
    *transitionIdOut = (muiTransitionId){0, 0};
    uint32_t set = class->sets[variant];
    for (uint32_t i = 0; set != 0 && i < store->sets[set - 1].bindingCount; i++)
    {
        const muiTransitionBinding* binding = &store->sets[set - 1].bindings[i];
        if (muiHasProperty(binding->properties, property))
        {
            *transitionIdOut = binding->transition;
        }
    }
    return mui_success;
}

bool muiNode_IsTransitioning(const muiContext* context, muiNodeId nodeId, muiProperty property)
{
    uint32_t slot = context != nullptr ? muiTreeResolve(&context->tree, nodeId) : 0;
    if (slot == 0 || !muiIsPropertyKnown(property))
    {
        return false;
    }
    for (uint32_t at = context->style.nodes[slot - 1].firstAnimation; at != 0;
         at = context->animations.records[at - 1].next)
    {
        if (context->animations.records[at - 1].property == property)
        {
            return true;
        }
    }
    return false;
}
