// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding a token and resolving its aliases. A cycle is refused when an
// alias is set, so resolution needs no check of its own.

#include "token_store.h"

#include "invariant.h"

uint32_t muiFindToken(const muiTokenStore* store, muiTokenId id)
{
    return muiPoolResolve(&store->pool, id.index1, id.generation);
}

const muiTokenValue* muiResolveToken(const muiTokenStore* store, muiTokenId id)
{
    for (uint32_t steps = 0; steps <= store->pool.capacity; steps++)
    {
        uint32_t slot = muiFindToken(store, id);
        if (slot == 0)
        {
            return nullptr;
        }
        const muiToken* token = &store->tokens[slot - 1];
        if (token->alias.index1 == 0)
        {
            return &token->value;
        }
        id = token->alias;
    }
    MUI_ASSERT(false);
    return nullptr;
}
