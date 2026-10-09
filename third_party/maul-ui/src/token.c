// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over tokens and the class variants that name them
// (record mui-0004). A cycle is refused when an alias is set, by walking
// from its target. A set keeps its names as a list; a property is in the
// set's values or its names, never both.

#include "maul-ui/token.h"

#include "condition.h"
#include "context.h"
#include "pool.h"
#include "property.h"
#include "style_edit.h"
#include "style_store.h"
#include "token_store.h"

#define TOKEN_DEF_COOKIE 0x6D75746Bu // "mutk"

// Whether the aliases from target lead to slot.
static bool LeadsTo(const muiTokenStore* store, muiTokenId target, uint32_t slot)
{
    for (uint32_t at = muiFindToken(store, target); at != 0;
         at = muiFindToken(store, store->tokens[at - 1].alias))
    {
        if (at == slot)
        {
            return true;
        }
    }
    return false;
}

muiTokenDef muiDefaultTokenDef(void)
{
    return (muiTokenDef){.cookie = TOKEN_DEF_COOKIE, .value = {.type = mui_tokenNumber}};
}

muiResult muiCreateToken(muiContext* context, const muiTokenDef* def, muiTokenId* tokenIdOut)
{
    if (tokenIdOut != nullptr)
    {
        *tokenIdOut = (muiTokenId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (def == nullptr || tokenIdOut == nullptr || def->cookie != TOKEN_DEF_COOKIE ||
        !muiIsTokenValueValid(&def->value) || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiTokenStore* store = &context->tokens;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    store->tokens[slot - 1] = (muiToken){.value = def->value};
    *tokenIdOut = (muiTokenId){slot, muiPoolGeneration(&store->pool, slot)};
    // A new token is named by nothing yet, so no node restyles.
    return mui_success;
}

// The slot of a live token for an edit, or 0 with the status.
static uint32_t ResolveEdit(muiContext* context, muiTokenId tokenId, muiResult* statusOut)
{
    if (tokenId.index1 == 0 || muiIsInHostCall(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiFindToken(&context->tokens, tokenId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiDestroyToken(muiContext* context, muiTokenId tokenId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, tokenId, &status);
    if (slot != 0)
    {
        muiPoolGive(&context->tokens.pool, slot);
        muiRestyleAll(context);
    }
    return status;
}

muiResult muiSetTokenValue(muiContext* context, muiTokenId tokenId, const muiTokenValue* value)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (value == nullptr || !muiIsTokenValueValid(value))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, tokenId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiToken* token = &context->tokens.tokens[slot - 1];
    if (value->type != token->value.type)
    {
        return muiRefuse(context);
    }
    *token = (muiToken){.value = *value};
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiSetTokenAlias(muiContext* context, muiTokenId tokenId, muiTokenId target)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (target.index1 == 0)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, tokenId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiTokenStore* store = &context->tokens;
    uint32_t targetSlot = muiFindToken(store, target);
    if (targetSlot == 0)
    {
        return mui_errorStale;
    }
    muiToken* token = &store->tokens[slot - 1];
    if (store->tokens[targetSlot - 1].value.type != token->value.type ||
        LeadsTo(store, target, slot))
    {
        return muiRefuse(context);
    }
    token->alias = target;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiGetTokenValue(const muiContext* context, muiTokenId tokenId, muiTokenValue* valueOut,
                           muiTokenId* aliasOut)
{
    if (context == nullptr || valueOut == nullptr || tokenId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    const muiTokenStore* store = &context->tokens;
    uint32_t slot = muiFindToken(store, tokenId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    const muiToken* token = &store->tokens[slot - 1];
    if (aliasOut != nullptr)
    {
        *aliasOut = token->alias;
    }
    const muiTokenValue* value = muiResolveToken(store, tokenId);
    if (value == nullptr)
    {
        *valueOut = (muiTokenValue){.type = token->value.type};
        return mui_empty;
    }
    *valueOut = *value;
    return mui_success;
}

// Names token for property in set, replacing its value or name; false
// when the context has no name left.
static bool Name(muiStyleStore* store, muiPropertySet* set, muiProperty property, muiTokenId token)
{
    const muiPropertyBits bit = muiPropertyOf(property);
    for (uint32_t at = set->firstTokenName; muiIntersects(set->tokens, bit) && at != 0;
         at = store->names[at - 1].next)
    {
        if (store->names[at - 1].property == property)
        {
            store->names[at - 1].token = token;
            return true;
        }
    }
    uint32_t name = muiPoolTake(&store->namePool);
    if (name == 0)
    {
        return false;
    }
    store->names[name - 1] = (muiTokenName){token, set->firstTokenName, property};
    set->firstTokenName = name;
    set->tokens = muiUnion(set->tokens, bit);
    set->properties = muiWithout(set->properties, bit);
    return true;
}

// Whether a variant of a class may read token for property: a property
// of the token's type that the variant's condition does not read.
static bool MayName(const muiContext* context, const muiStyleClass* class, muiVariant variant,
                    muiProperty property, muiTokenId token)
{
    muiTokenType type = muiPropertyTokenType(property);
    if (type == 0)
    {
        return false;
    }
    if (variant >= mui_variantCondition0 &&
        muiHasProperty(muiForbiddenProperties(&class->conditions[variant - mui_variantCondition0]),
                       property))
    {
        return false;
    }
    uint32_t slot = muiFindToken(&context->tokens, token);
    return slot == 0 || context->tokens.tokens[slot - 1].value.type == type;
}

muiResult muiStyle_SetToken(muiContext* context, muiStyleId styleId, muiVariant variant,
                            muiProperty property, muiTokenId tokenId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (!muiIsPropertyKnown(property))
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
    if (!muiHasVariant(class, variant) || !MayName(context, class, variant, property, tokenId))
    {
        return muiRefuse(context);
    }
    if (tokenId.index1 == 0)
    {
        if (class->sets[variant] != 0)
        {
            muiDropTokenNames(store, &store->sets[class->sets[variant] - 1],
                              muiPropertyOf(property));
            muiReleaseEmptySet(store, class, variant);
        }
        muiRestyleAll(context);
        return mui_success;
    }
    if (muiFindToken(&context->tokens, tokenId) == 0)
    {
        return mui_errorStale;
    }
    muiPropertySet* set = muiTakeVariantSet(store, class, variant);
    if (set == nullptr)
    {
        return mui_errorCapacity;
    }
    if (!Name(store, set, property, tokenId))
    {
        muiReleaseEmptySet(store, class, variant);
        return mui_errorCapacity;
    }
    store->reach = muiUnion(store->reach, muiPropertyOf(property));
    context->textGiven |= MUI_PROPERTY_GROUP(property) == mui_groupText;
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiStyle_GetToken(const muiContext* context, muiStyleId styleId, muiVariant variant,
                            muiProperty property, muiTokenId* tokenIdOut)
{
    if (context == nullptr || tokenIdOut == nullptr || styleId.index1 == 0 ||
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
    *tokenIdOut = (muiTokenId){0, 0};
    uint32_t set = class->sets[variant];
    for (uint32_t at = set != 0 ? store->sets[set - 1].firstTokenName : 0; at != 0;
         at = store->names[at - 1].next)
    {
        if (store->names[at - 1].property == property)
        {
            *tokenIdOut = store->names[at - 1].token;
        }
    }
    return mui_success;
}
