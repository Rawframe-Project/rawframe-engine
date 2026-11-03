// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding overrides and resolving tokens through a scope's themes.

#include "theme_store.h"

#include "invariant.h"

uint32_t* muiThemeEntry(const muiThemeStore* store, uint32_t theme, uint32_t tokenSlot)
{
    return &store->tables[(size_t)(theme - 1) * store->tokenCapacity + (tokenSlot - 1)];
}

const muiThemeOverride* muiFindOverride(const muiThemeStore* store, uint32_t theme,
                                        muiTokenId token)
{
    uint32_t record = *muiThemeEntry(store, theme, token.index1);
    // The generation tells this token from a later one in its slot.
    if (record == 0 || store->overrides[record - 1].token.generation != token.generation)
    {
        return nullptr;
    }
    return &store->overrides[record - 1];
}

const muiTokenValue* muiResolveTokenIn(const muiTokenStore* tokens, const muiThemeStore* themes,
                                       const muiThemeScope* scope, muiTokenId id)
{
    for (uint32_t steps = 0; steps <= tokens->pool.capacity; steps++)
    {
        uint32_t slot = muiFindToken(tokens, id);
        if (slot == 0)
        {
            return nullptr;
        }
        const muiThemeOverride* found = nullptr;
        for (uint32_t i = 0; found == nullptr && i < scope->count; i++)
        {
            found = muiFindOverride(themes, scope->themes[i], id);
        }
        muiTokenId alias = found != nullptr ? found->alias : tokens->tokens[slot - 1].alias;
        if (alias.index1 == 0)
        {
            return found != nullptr ? &found->value : &tokens->tokens[slot - 1].value;
        }
        id = alias;
    }
    MUI_ASSERT(false);
    return nullptr;
}

// True when id is token; else puts a live token not yet reached on the
// stack at top.
static bool Reach(muiTokenStore* tokens, muiTokenId id, muiTokenId token, uint32_t* top)
{
    if (id.index1 == token.index1 && id.generation == token.generation)
    {
        return true;
    }
    uint32_t slot = muiFindToken(tokens, id);
    if (slot != 0 && !tokens->tokens[slot - 1].reached)
    {
        tokens->tokens[slot - 1].reached = true;
        tokens->tokens[slot - 1].next = *top;
        *top = slot;
    }
    return false;
}

bool muiAliasLeadsTo(muiTokenStore* tokens, const muiThemeStore* themes, muiTokenId target,
                     muiTokenId token)
{
    for (uint32_t slot = 1; slot <= tokens->pool.used; slot++)
    {
        tokens->tokens[slot - 1].reached = false;
    }
    uint32_t top = 0;
    if (Reach(tokens, target, token, &top))
    {
        return true;
    }
    // Each token is put on the stack once, so the walk takes at most the
    // tokens times the themes and one steps.
    while (top != 0)
    {
        uint32_t slot = top;
        top = tokens->tokens[slot - 1].next;
        if (Reach(tokens, tokens->tokens[slot - 1].alias, token, &top))
        {
            return true;
        }
        muiTokenId id = {.index1 = slot, .generation = tokens->pool.slots[slot - 1].generation};
        for (uint32_t theme = 1; theme <= themes->pool.used; theme++)
        {
            const muiThemeOverride* found =
                themes->pool.slots[theme - 1].live ? muiFindOverride(themes, theme, id) : nullptr;
            if (found != nullptr && Reach(tokens, found->alias, token, &top))
            {
                return true;
            }
        }
    }
    return false;
}
