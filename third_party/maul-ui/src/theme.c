// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public functions over themes and the nodes they are set on (record
// mui-0004). A cycle through a theme's own aliases and the context's is
// refused when an alias is set; one that only nested themes close is
// bounded when a token is read.

#include "maul-ui/theme.h"

#include "context.h"
#include "pool.h"
#include "property.h"
#include "restyle.h"
#include "style_edit.h"
#include "style_store.h"
#include "theme_store.h"
#include "token_store.h"
#include "tree.h"

muiResult muiCreateTheme(muiContext* context, muiThemeId* themeIdOut)
{
    if (themeIdOut != nullptr)
    {
        *themeIdOut = (muiThemeId){0, 0};
    }
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (themeIdOut == nullptr || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiThemeStore* store = &context->themes;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    *themeIdOut = (muiThemeId){slot, muiPoolGeneration(&store->pool, slot)};
    // Set on no node yet, so no node restyles.
    return mui_success;
}

// The slot of a live theme for an edit, or 0 with the status.
static uint32_t ResolveEdit(muiContext* context, muiThemeId themeId, muiResult* statusOut)
{
    if (themeId.index1 == 0 || muiIsInHostCall(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiPoolResolve(&context->themes.pool, themeId.index1, themeId.generation);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiDestroyTheme(muiContext* context, muiThemeId themeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = ResolveEdit(context, themeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiThemeStore* store = &context->themes;
    for (uint32_t token = 1; token <= store->tokenCapacity; token++)
    {
        uint32_t* entry = muiThemeEntry(store, slot, token);
        if (*entry != 0)
        {
            muiPoolGive(&store->overridePool, *entry);
            *entry = 0;
        }
    }
    muiPoolGive(&store->pool, slot);
    muiRestyleAll(context);
    return mui_success;
}

// The theme's override record for a live token, made if it has none; the
// token's slot in tokenOut. 0 with the status on failure.
static uint32_t TakeOverride(muiContext* context, muiThemeId themeId, muiTokenId tokenId,
                             muiResult* statusOut)
{
    uint32_t theme = ResolveEdit(context, themeId, statusOut);
    if (theme == 0)
    {
        return 0;
    }
    if (tokenId.index1 == 0)
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    if (muiFindToken(&context->tokens, tokenId) == 0)
    {
        *statusOut = mui_errorStale;
        return 0;
    }
    muiThemeStore* store = &context->themes;
    uint32_t* entry = muiThemeEntry(store, theme, tokenId.index1);
    // A record of a gone token in the slot is taken over.
    if (*entry == 0)
    {
        *entry = muiPoolTake(&store->overridePool);
        if (*entry == 0)
        {
            *statusOut = mui_errorCapacity;
            return 0;
        }
    }
    return *entry;
}

muiResult muiTheme_SetTokenValue(muiContext* context, muiThemeId themeId, muiTokenId tokenId,
                                 const muiTokenValue* value)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (value == nullptr || !muiIsTokenValueValid(value))
    {
        return muiRefuse(context);
    }
    uint32_t token = muiFindToken(&context->tokens, tokenId);
    if (token != 0 && context->tokens.tokens[token - 1].value.type != value->type)
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t record = TakeOverride(context, themeId, tokenId, &status);
    if (record == 0)
    {
        return status;
    }
    context->themes.overrides[record - 1] = (muiThemeOverride){.token = tokenId, .value = *value};
    muiRestyleAll(context);
    return mui_success;
}

// Whether the aliases from target, read through one theme and the
// context, lead to token.
static bool LeadsTo(const muiContext* context, uint32_t theme, muiTokenId target, muiTokenId token)
{
    const muiTokenStore* tokens = &context->tokens;
    muiTokenId id = target;
    for (uint32_t steps = 0; steps <= tokens->pool.capacity; steps++)
    {
        if (id.index1 == token.index1 && id.generation == token.generation)
        {
            return true;
        }
        uint32_t slot = muiFindToken(tokens, id);
        if (slot == 0)
        {
            return false;
        }
        const muiThemeOverride* found = muiFindOverride(&context->themes, theme, id);
        id = found != nullptr ? found->alias : tokens->tokens[slot - 1].alias;
        if (id.index1 == 0)
        {
            return false;
        }
    }
    return true;
}

muiResult muiTheme_SetTokenAlias(muiContext* context, muiThemeId themeId, muiTokenId tokenId,
                                 muiTokenId target)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (target.index1 == 0 || tokenId.index1 == 0)
    {
        return muiRefuse(context);
    }
    const muiTokenStore* tokens = &context->tokens;
    uint32_t token = muiFindToken(tokens, tokenId);
    uint32_t targetSlot = muiFindToken(tokens, target);
    uint32_t theme = muiPoolResolve(&context->themes.pool, themeId.index1, themeId.generation);
    if (token == 0 || targetSlot == 0 || theme == 0)
    {
        return mui_errorStale;
    }
    if (tokens->tokens[targetSlot - 1].value.type != tokens->tokens[token - 1].value.type ||
        LeadsTo(context, theme, target, tokenId))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t record = TakeOverride(context, themeId, tokenId, &status);
    if (record == 0)
    {
        return status;
    }
    context->themes.overrides[record - 1] = (muiThemeOverride){
        .token = tokenId,
        .alias = target,
        .value = {.type = tokens->tokens[token - 1].value.type},
    };
    muiRestyleAll(context);
    return mui_success;
}

muiResult muiTheme_ResetToken(muiContext* context, muiThemeId themeId, muiTokenId tokenId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t theme = ResolveEdit(context, themeId, &status);
    if (theme == 0)
    {
        return status;
    }
    if (tokenId.index1 == 0)
    {
        return muiRefuse(context);
    }
    if (muiFindToken(&context->tokens, tokenId) == 0)
    {
        return mui_errorStale;
    }
    muiThemeStore* store = &context->themes;
    uint32_t* entry = muiThemeEntry(store, theme, tokenId.index1);
    if (*entry != 0)
    {
        muiPoolGive(&store->overridePool, *entry);
        *entry = 0;
        muiRestyleAll(context);
    }
    return mui_success;
}

muiResult muiTheme_GetToken(const muiContext* context, muiThemeId themeId, muiTokenId tokenId,
                            muiTokenValue* valueOut, muiTokenId* aliasOut)
{
    if (context == nullptr || valueOut == nullptr || themeId.index1 == 0 || tokenId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t theme = muiPoolResolve(&context->themes.pool, themeId.index1, themeId.generation);
    uint32_t token = muiFindToken(&context->tokens, tokenId);
    if (theme == 0 || token == 0)
    {
        return mui_errorStale;
    }
    const muiThemeOverride* found = muiFindOverride(&context->themes, theme, tokenId);
    if (aliasOut != nullptr)
    {
        *aliasOut = found != nullptr ? found->alias : (muiTokenId){0, 0};
    }
    *valueOut = found != nullptr
                    ? found->value
                    : (muiTokenValue){.type = context->tokens.tokens[token - 1].value.type};
    return found != nullptr ? mui_success : mui_empty;
}

muiResult muiNode_SetTheme(muiContext* context, muiNodeId nodeId, muiThemeId themeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    if (themeId.index1 != 0 &&
        muiPoolResolve(&context->themes.pool, themeId.index1, themeId.generation) == 0)
    {
        return mui_errorStale;
    }
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    node->theme = themeId;
    node->edited = true;
    muiTreeMark(&context->tree, slot, mui_stageStyle);
    return mui_success;
}

muiResult muiNode_GetTheme(const muiContext* context, muiNodeId nodeId, muiThemeId* themeIdOut)
{
    if (context == nullptr || themeIdOut == nullptr || nodeId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *themeIdOut = context->style.nodes[slot - 1].theme;
    return mui_success;
}

muiResult muiNode_GetTokenValue(const muiContext* context, muiNodeId nodeId, muiTokenId tokenId,
                                muiTokenValue* valueOut)
{
    if (context == nullptr || valueOut == nullptr || nodeId.index1 == 0 || tokenId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    uint32_t token = muiFindToken(&context->tokens, tokenId);
    if (slot == 0 || token == 0)
    {
        return mui_errorStale;
    }
    const muiThemeScope scope = muiScopeOf(context, slot);
    const muiTokenValue* value =
        muiResolveTokenIn(&context->tokens, &context->themes, &scope, tokenId);
    if (value == nullptr)
    {
        *valueOut = (muiTokenValue){.type = context->tokens.tokens[token - 1].value.type};
        return mui_empty;
    }
    *valueOut = *value;
    return mui_success;
}
