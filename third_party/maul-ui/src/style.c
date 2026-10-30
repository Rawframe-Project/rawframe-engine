// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over style classes and node types. Any change to
// one restyles every node at the next layout: classes change rarely, as a
// theme does, and no index from a class to its nodes has to be kept.

#include "maul-ui/style.h"

#include "condition.h"
#include "context.h"
#include "fpenv.h"
#include "pool.h"
#include "property.h"
#include "style_edit.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/text_style.h"
#include "maul-ui/visual.h"

#include <string.h>

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

static muiResult CreateStyle(muiContext* context, const muiStyleDef* def, muiStyleId* styleOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || styleOut == nullptr || def->cookie != STYLE_DEF_COOKIE)
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
    *styleOut = (muiStyleId){slot, muiPoolGeneration(&store->classPool, slot)};
    return mui_success;
}

muiResult muiCreateStyle(muiContext* context, const muiStyleDef* def, muiStyleId* styleOut)
{
    if (styleOut != nullptr)
    {
        memset(styleOut, 0, sizeof *styleOut);
    }
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = CreateStyle(context, def, styleOut);
        muiLeave(context);
    }
    return status;
}

static muiResult DestroyStyle(muiContext* context, muiStyleId styleId)
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

muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = DestroyStyle(context, styleId);
        muiLeave(context);
    }
    return status;
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
    if ((mask & ~allowed) != 0)
    {
        return muiRefuse(context);
    }
    muiResult valuesStatus = muiPropertiesStatus(values, properties);
    if (valuesStatus != mui_success)
    {
        return muiRefuseAs(context, valuesStatus);
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

static muiResult StyleSetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant,
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

static MUI_NOINLINE muiResult StyleSetLayoutValuesWorker(muiContext* context, muiStyleId styleId,
                                                         muiVariant variant,
                                                         const muiLayoutStyle* values,
                                                         muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleSetLayoutValues(context, styleId, variant, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_SetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   const muiLayoutStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleSetLayoutValuesWorker(context, styleId, variant, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult StyleSetVisualValues(muiContext* context, muiStyleId styleId, muiVariant variant,
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

static MUI_NOINLINE muiResult StyleSetVisualValuesWorker(muiContext* context, muiStyleId styleId,
                                                         muiVariant variant,
                                                         const muiVisualStyle* values,
                                                         muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleSetVisualValues(context, styleId, variant, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_SetVisualValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   const muiVisualStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleSetVisualValuesWorker(context, styleId, variant, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult StyleResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant,
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

muiResult muiStyle_ResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant,
                                   muiPropertyGroup group, muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleResetProperties(context, styleId, variant, group, mask);
        muiLeave(context);
    }
    return status;
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
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (maskOut != nullptr)
    {
        *maskOut = 0;
    }
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiLayoutStyle values = muiDefaultLayoutStyle();
    muiResult result =
        GetValues(context, styleId, variant, (muiValuesRef){&values, nullptr, nullptr, nullptr},
                  mui_groupLayout, maskOut);
    if (result == mui_success)
    {
        *valuesOut = values;
    }
    return result;
}

muiResult muiStyle_GetVisualValues(const muiContext* context, muiStyleId styleId,
                                   muiVariant variant, muiVisualStyle* valuesOut,
                                   muiPropertyMask* maskOut)
{
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (maskOut != nullptr)
    {
        *maskOut = 0;
    }
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiVisualStyle values = muiDefaultVisualStyle();
    muiResult result =
        GetValues(context, styleId, variant, (muiValuesRef){nullptr, &values, nullptr, nullptr},
                  mui_groupVisual, maskOut);
    if (result == mui_success)
    {
        *valuesOut = values;
    }
    return result;
}

static muiResult StyleSetTextValues(muiContext* context, muiStyleId styleId, muiVariant variant,
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

static MUI_NOINLINE muiResult StyleSetTextValuesWorker(muiContext* context, muiStyleId styleId,
                                                       muiVariant variant,
                                                       const muiTextStyle* values,
                                                       muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleSetTextValues(context, styleId, variant, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_SetTextValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                 const muiTextStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleSetTextValuesWorker(context, styleId, variant, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiStyle_GetTextValues(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                 muiTextStyle* valuesOut, muiPropertyMask* maskOut)
{
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (maskOut != nullptr)
    {
        *maskOut = 0;
    }
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiTextStyle values = muiDefaultTextStyle();
    muiResult result =
        GetValues(context, styleId, variant, (muiValuesRef){nullptr, nullptr, &values, nullptr},
                  mui_groupText, maskOut);
    if (result == mui_success)
    {
        *valuesOut = values;
    }
    return result;
}

static muiResult StyleSetInteractionValues(muiContext* context, muiStyleId styleId,
                                           muiVariant variant, const muiInteractionStyle* values,
                                           muiPropertyMask mask)
{
    if (values == nullptr)
    {
        return context != nullptr ? muiRefuse(context) : mui_errorInvalid;
    }
    return SetValues(context, styleId, variant,
                     (muiConstValuesRef){nullptr, nullptr, nullptr, values}, mui_groupInteraction,
                     mask, MUI_INTERACTION_PROPERTIES);
}

static MUI_NOINLINE muiResult StyleSetInteractionValuesWorker(muiContext* context,
                                                              muiStyleId styleId,
                                                              muiVariant variant,
                                                              const muiInteractionStyle* values,
                                                              muiPropertyMask mask)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleSetInteractionValues(context, styleId, variant, values, mask);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_SetInteractionValues(muiContext* context, muiStyleId styleId, muiVariant variant,
                                        const muiInteractionStyle* values, muiPropertyMask mask)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleSetInteractionValuesWorker(context, styleId, variant, values, mask);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiStyle_GetInteractionValues(const muiContext* context, muiStyleId styleId,
                                        muiVariant variant, muiInteractionStyle* valuesOut,
                                        muiPropertyMask* maskOut)
{
    if (valuesOut != nullptr)
    {
        memset(valuesOut, 0, sizeof *valuesOut);
    }
    if (maskOut != nullptr)
    {
        *maskOut = 0;
    }
    if (valuesOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiInteractionStyle values = muiDefaultInteractionStyle();
    muiResult result =
        GetValues(context, styleId, variant, (muiValuesRef){nullptr, nullptr, nullptr, &values},
                  mui_groupInteraction, maskOut);
    if (result == mui_success)
    {
        *valuesOut = values;
    }
    return result;
}

muiVisualStyle muiDefaultVisualStyle(void)
{
    return *muiVisualDefaults();
}

static muiResult StyleAddCondition(muiContext* context, muiStyleId styleId,
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
        return muiRefuseLimit(context);
    }
    class->conditions[class->conditionCount] = *condition;
    *variantOut = (muiVariant)(mui_variantCondition0 + class->conditionCount);
    class->conditionCount++;
    muiRestyleAll(context);
    return mui_success;
}

static MUI_NOINLINE muiResult StyleAddConditionWorker(muiContext* context, muiStyleId styleId,
                                                      const muiCondition* condition,
                                                      muiVariant* variantOut)
{
    if (variantOut != nullptr)
    {
        *variantOut = 0;
    }
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleAddCondition(context, styleId, condition, variantOut);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_AddCondition(muiContext* context, muiStyleId styleId,
                                const muiCondition* condition, muiVariant* variantOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleAddConditionWorker(context, styleId, condition, variantOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult StyleSetCondition(muiContext* context, muiStyleId styleId, muiVariant variant,
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

static MUI_NOINLINE muiResult StyleSetConditionWorker(muiContext* context, muiStyleId styleId,
                                                      muiVariant variant,
                                                      const muiCondition* condition)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleSetCondition(context, styleId, variant, condition);
        muiLeave(context);
    }
    return status;
}

muiResult muiStyle_SetCondition(muiContext* context, muiStyleId styleId, muiVariant variant,
                                const muiCondition* condition)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = StyleSetConditionWorker(context, styleId, variant, condition);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiStyle_GetCondition(const muiContext* context, muiStyleId styleId, muiVariant variant,
                                muiCondition* conditionOut)
{
    if (conditionOut != nullptr)
    {
        memset(conditionOut, 0, sizeof *conditionOut);
    }
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

static muiResult StyleClearConditions(muiContext* context, muiStyleId styleId)
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

muiResult muiStyle_ClearConditions(muiContext* context, muiStyleId styleId)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = StyleClearConditions(context, styleId);
        muiLeave(context);
    }
    return status;
}

static muiResult SetContextEnvironment(muiContext* context, const muiEnvironment* environment)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (environment == nullptr || !muiIsEnvironmentValid(environment))
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

static MUI_NOINLINE muiResult SetContextEnvironmentWorker(muiContext* context,
                                                          const muiEnvironment* environment)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = SetContextEnvironment(context, environment);
        muiLeave(context);
    }
    return status;
}

muiResult muiContext_SetEnvironment(muiContext* context, const muiEnvironment* environment)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = SetContextEnvironmentWorker(context, environment);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiEnvironment muiContext_GetEnvironment(const muiContext* context)
{
    return context != nullptr ? context->environment : muiDefaultEnvironment();
}

muiNodeTypeDef muiDefaultNodeTypeDef(void)
{
    return (muiNodeTypeDef){.cookie = NODE_TYPE_DEF_COOKIE};
}

static muiResult CreateNodeType(muiContext* context, const muiNodeTypeDef* def,
                                muiNodeTypeId* typeOut)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || typeOut == nullptr || def->cookie != NODE_TYPE_DEF_COOKIE)
    {
        return muiRefuse(context);
    }
    muiResult listStatus = muiClassListStatus(def->classes, def->classCount);
    if (listStatus != mui_success)
    {
        return muiRefuseAs(context, listStatus);
    }
    muiStyleStore* store = &context->style;
    uint32_t slot = muiPoolTake(&store->typePool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    muiSetClassList(&store->types[slot - 1], def->classes, def->classCount);
    // No node can have a type that did not exist, so none restyles.
    *typeOut = (muiNodeTypeId){slot, muiPoolGeneration(&store->typePool, slot)};
    return mui_success;
}

muiResult muiCreateNodeType(muiContext* context, const muiNodeTypeDef* def, muiNodeTypeId* typeOut)
{
    if (typeOut != nullptr)
    {
        memset(typeOut, 0, sizeof *typeOut);
    }
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = CreateNodeType(context, def, typeOut);
        muiLeave(context);
    }
    return status;
}

// The slot of a live node type for an edit, or 0 with the status in
// statusOut.
static uint32_t ResolveTypeEdit(muiContext* context, muiNodeTypeId typeId, muiResult* statusOut)
{
    if (typeId.index1 == 0)
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->style.typePool, typeId.index1, typeId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

static muiResult DestroyNodeType(muiContext* context, muiNodeTypeId typeId)
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

muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = DestroyNodeType(context, typeId);
        muiLeave(context);
    }
    return status;
}

static muiResult NodeTypeSetClasses(muiContext* context, muiNodeTypeId typeId,
                                    const muiStyleId* classes, uint32_t count)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult listStatus = muiClassListStatus(classes, count);
    if (listStatus != mui_success)
    {
        return muiRefuseAs(context, listStatus);
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

muiResult muiNodeType_SetClasses(muiContext* context, muiNodeTypeId typeId,
                                 const muiStyleId* classes, uint32_t count)
{
    muiResult status = muiEnter(context);
    if (status == mui_success)
    {
        status = NodeTypeSetClasses(context, typeId, classes, count);
        muiLeave(context);
    }
    return status;
}
