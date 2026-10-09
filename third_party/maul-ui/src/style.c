// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over style classes and node types. Any change to
// one restyles every node at the next layout (record mui-0004): classes
// change rarely, as a theme does, and no index from a class to its nodes
// has to be kept.

#include "maul-ui/style.h"

#include "condition.h"
#include "context.h"
#include "pool.h"
#include "property.h"
#include "style_edit.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/text_style.h"
#include "maul-ui/visual.h"

#define STYLE_DEF_COOKIE     0x6D757374u // "must"
#define NODE_TYPE_DEF_COOKIE 0x6D756E74u // "munt"

// The condition of a variant that has one, or NULL.
static const muiCondition* ConditionOf(const muiStyleClass* class, muiVariant variant)
{
    return variant >= mui_variantCondition0 && muiHasVariant(class, variant)
               ? &class->conditions[variant - mui_variantCondition0]
               : nullptr;
}

muiStyleDef muiDefaultStyleDef(void)
{
    return (muiStyleDef){.cookie = STYLE_DEF_COOKIE};
}

muiResult muiCreateStyle(muiContext* context, const muiStyleDef* def, muiStyleId* styleIdOut)
{
    if (styleIdOut != nullptr)
    {
        *styleIdOut = (muiStyleId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || styleIdOut == nullptr || def->cookie != STYLE_DEF_COOKIE ||
        muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolTake(&store->classPool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    store->classes[slot - 1] = (muiStyleClass){0};
    // No node can list a class that did not exist, so none restyles.
    *styleIdOut = (muiStyleId){slot, muiPoolGeneration(&store->classPool, slot)};
    return mui_success;
}

muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    for (uint32_t v = 0; v < MUI_VARIANT_SLOTS; v++)
    {
        uint32_t set = store->classes[slot - 1].sets[v];
        if (set != 0)
        {
            muiFreeSet(store, set);
        }
    }
    muiPoolGive(&store->classPool, slot);
    muiRestyleAll(context);
    return mui_success;
}

// Sets the properties of a group mask names, within allowed, from
// values.
static muiResult SetValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                           muiConstValuesRef values, muiPropertyGroup group, muiPropertyMask mask,
                           muiPropertyMask allowed)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiPropertyBits properties = muiPropertiesOf(group, mask);
    if ((mask & ~allowed) != 0 || !muiArePropertiesValid(values, properties))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    const muiCondition* condition = ConditionOf(class, variant);
    if (!muiHasVariant(class, variant) ||
        (condition != nullptr && muiIntersects(properties, muiForbiddenProperties(condition))))
    {
        return muiRefuse(context);
    }
    muiPropertySet* target = muiTakeVariantSet(store, class, variant);
    if (target == nullptr)
    {
        return mui_errorCapacity;
    }
    muiApplyProperties(muiRefOf(&target->values), values, properties);
    muiDropTokenNames(store, target, properties);
    target->properties = muiUnion(target->properties, properties);
    store->reach = muiUnion(store->reach, properties);
    context->textGiven |= group == mui_groupText && mask != 0;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_SetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   const muiLayoutStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetValues(context, styleId, variant,
                     (muiConstValuesRef){values, nullptr, nullptr, nullptr}, mui_groupLayout, mask,
                     MUI_LAYOUT_PROPERTIES);
}

muiResult muiStyle_SetVisualValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   const muiVisualStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetValues(context, styleId, variant,
                     (muiConstValuesRef){nullptr, values, nullptr, nullptr}, mui_groupVisual, mask,
                     MUI_VISUAL_PROPERTIES);
}

muiResult muiStyle_ResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   muiPropertyGroup group, muiPropertyMask mask)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsGroupMaskKnown(group, mask))
    {
        return muiRefuse(context);
    }
    const muiPropertyBits properties = muiPropertiesOf(group, mask);
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    if (!muiHasVariant(&store->classes[slot - 1], variant))
    {
        return muiRefuse(context);
    }
    muiStyleClass* class = &store->classes[slot - 1];
    if (class->sets[variant] != 0)
    {
        muiPropertySet* set = &store->sets[class->sets[variant] - 1];
        set->properties = muiWithout(set->properties, properties);
        muiDropTokenNames(store, set, properties);
        muiReleaseEmptySet(store, class, variant);
    }
    muiRestyleAll(context);
    return mui_success;
}

// Reads the values a variant sets among allowed into out, which holds the
// defaults for the rest.
static muiResult GetValues(const muiContext* context, muiStyleId styleId, muiVariant variant,
                           muiValuesRef out, muiPropertyGroup group, muiPropertyMask* maskOut)
{
    if (context == nullptr || maskOut == nullptr || styleId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    if (!muiHasVariant(&store->classes[slot - 1], variant))
    {
        return mui_errorInvalid;
    }
    *maskOut = 0;
    uint32_t set = store->classes[slot - 1].sets[variant];
    if (set != 0)
    {
        *maskOut = store->sets[set - 1].properties.words[group];
        muiApplyProperties(out, muiConstRefOf(&store->sets[set - 1].values),
                           muiPropertiesOf(group, *maskOut));
    }
    return mui_success;
}

muiResult muiStyle_GetLayoutValues(const muiContext* context, muiStyleId styleId,
                                   muiVariant variant, muiLayoutStyle* valuesOut,
                                   muiPropertyMask* maskOut)
{
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    *valuesOut = muiDefaultLayoutStyle();
    return GetValues(context, styleId, variant,
                     (muiValuesRef){valuesOut, nullptr, nullptr, nullptr}, mui_groupLayout,
                     maskOut);
}

muiResult muiStyle_GetVisualValues(const muiContext* context, muiStyleId styleId,
                                   muiVariant variant, muiVisualStyle* valuesOut,
                                   muiPropertyMask* maskOut)
{
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    *valuesOut = muiDefaultVisualStyle();
    return GetValues(context, styleId, variant,
                     (muiValuesRef){nullptr, valuesOut, nullptr, nullptr}, mui_groupVisual,
                     maskOut);
}

muiResult muiStyle_SetTextValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                 const muiTextStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetValues(context, styleId, variant,
                     (muiConstValuesRef){nullptr, nullptr, values, nullptr}, mui_groupText, mask,
                     MUI_TEXT_PROPERTIES);
}

muiResult muiStyle_GetTextValues(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                 muiTextStyle* valuesOut, muiPropertyMask* maskOut)
{
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    *valuesOut = muiDefaultTextStyle();
    return GetValues(context, styleId, variant,
                     (muiValuesRef){nullptr, nullptr, valuesOut, nullptr}, mui_groupText, maskOut);
}

muiResult muiStyle_SetInteractionValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                        const muiInteractionStyle* values, muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetValues(context, styleId, variant,
                     (muiConstValuesRef){nullptr, nullptr, nullptr, values}, mui_groupInteraction,
                     mask, MUI_INTERACTION_PROPERTIES);
}

muiResult muiStyle_GetInteractionValues(const muiContext* context, muiStyleId styleId,
                                        muiVariant variant, muiInteractionStyle* valuesOut,
                                        muiPropertyMask* maskOut)
{
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    *valuesOut = muiDefaultInteractionStyle();
    return GetValues(context, styleId, variant,
                     (muiValuesRef){nullptr, nullptr, nullptr, valuesOut}, mui_groupInteraction,
                     maskOut);
}

muiVisualStyle muiDefaultVisualStyle(void)
{
    return *muiVisualDefaults();
}

muiResult muiStyle_AddCondition(muiContext* context, muiStyleId styleId,
                                const muiCondition* condition, muiVariant* variantOut)
{
    if (variantOut != nullptr)
    {
        *variantOut = mui_variantBase;
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (condition == nullptr || variantOut == nullptr || !muiIsConditionValid(condition))
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
    if (class->conditionCount == MUI_MAX_CONDITIONS)
    {
        return mui_errorCapacity;
    }
    class->conditions[class->conditionCount] = *condition;
    *variantOut = (muiVariant)(mui_variantCondition0 + class->conditionCount);
    class->conditionCount++;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_SetCondition(muiContext* context, muiStyleId styleId, muiVariant variant,
                                const muiCondition* condition)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (condition == nullptr || !muiIsConditionValid(condition))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    if (ConditionOf(class, variant) == nullptr)
    {
        return muiRefuse(context);
    }
    uint32_t set = class->sets[variant];
    // The values set already may not be what the new condition reads.
    if (set != 0 &&
        muiIntersects(muiUnion(store->sets[set - 1].properties, store->sets[set - 1].tokens),
                      muiForbiddenProperties(condition)))
    {
        return muiRefuse(context);
    }
    class->conditions[variant - mui_variantCondition0] = *condition;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_GetCondition(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                muiCondition* conditionOut)
{
    if (context == nullptr || conditionOut == nullptr || styleId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    const muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolResolve(&store->classPool, styleId.index1, styleId.generation);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiCondition* condition = ConditionOf(&store->classes[slot - 1], variant);
    if (condition == nullptr)
    {
        return mui_errorInvalid;
    }
    *conditionOut = *condition;
    return mui_success;
}

muiResult muiStyle_ClearConditions(muiContext* context, muiStyleId styleId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveClassEdit(context, styleId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiStyleStore* store = &context->style;
    muiStyleClass* class = &store->classes[slot - 1];
    for (uint32_t i = 0; i < class->conditionCount; i++)
    {
        uint32_t* set = &class->sets[mui_variantCondition0 + i];
        if (*set != 0)
        {
            muiFreeSet(store, *set);
            *set = 0;
        }
    }
    class->conditionCount = 0;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiSetContextEnvironment(muiContext* context, const muiEnvironment* environment)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (environment == nullptr || !muiIsEnvironmentValid(environment) || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    const muiEnvironment* current = &context->environment;
    if (current->viewport != environment->viewport || current->input != environment->input ||
        current->textScale != environment->textScale ||
        current->reducedMotion != environment->reducedMotion)
    {
        context->environment = *environment;
        muiRestyleAll(context);
    }
    return mui_success;
}

muiEnvironment muiGetContextEnvironment(const muiContext* context)
{
    return context != nullptr ? context->environment : muiDefaultEnvironment();
}

muiNodeTypeDef muiDefaultNodeTypeDef(void)
{
    return (muiNodeTypeDef){.cookie = NODE_TYPE_DEF_COOKIE};
}

muiResult muiCreateNodeType(muiContext* context, const muiNodeTypeDef* def,
                            muiNodeTypeId* typeIdOut)
{
    if (typeIdOut != nullptr)
    {
        *typeIdOut = (muiNodeTypeId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || typeIdOut == nullptr || def->cookie != NODE_TYPE_DEF_COOKIE ||
        !muiIsClassListValid(def->classes, def->classCount) || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolTake(&store->typePool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    muiSetClassList(&store->types[slot - 1], def->classes, def->classCount);
    // No node can have a type that did not exist, so none restyles.
    *typeIdOut = (muiNodeTypeId){slot, muiPoolGeneration(&store->typePool, slot)};
    return mui_success;
}

// The slot of a live node type for an edit, or 0 with the status in
// statusOut.
static uint32_t ResolveTypeEdit(muiContext* context, muiNodeTypeId typeId, muiResult* statusOut)
{
    if (typeId.index1 == 0 || muiIsInHostCall(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->style.typePool, typeId.index1, typeId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveTypeEdit(context, typeId, &status);
    if (slot != 0)
    {
        muiPoolGive(&context->style.typePool, slot);
        muiRestyleAll(context);
    }
    return status;
}

muiResult muiNodeType_SetClasses(muiContext* context, muiNodeTypeId typeId,
                                 const muiStyleId* classes, uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsClassListValid(classes, count))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveTypeEdit(context, typeId, &status);
    if (slot != 0)
    {
        muiSetClassList(&context->style.types[slot - 1], classes, count);
        muiRestyleAll(context);
    }
    return status;
}
