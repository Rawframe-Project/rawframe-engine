// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding overrides and resolving tokens through a scope's themes.

#include "theme_store.h"

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
    return nullptr;
}
