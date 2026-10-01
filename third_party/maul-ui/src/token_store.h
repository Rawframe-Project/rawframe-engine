// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's tokens: each a typed value or an alias to a token of its
// type, never in a cycle (record mui-0004).

#ifndef MAUL_UI_SRC_TOKEN_STORE_H
#define MAUL_UI_SRC_TOKEN_STORE_H

#include "pool.h"

#include "maul-ui/token.h"

typedef struct muiToken
{
    // The literal, whose type is the token's for good; its member is
    // unused while the token is an alias.
    muiTokenValue value;
    // The token aliased, or the null id.
    muiTokenId alias;
} muiToken;

typedef struct muiTokenStore
{
    muiPool pool;
    muiToken* tokens;
} muiTokenStore;

// The slot of a live token, or 0.
uint32_t muiFindToken(const muiTokenStore* store, muiTokenId id);

// A token's value through its aliases, or NULL when the token, or one
// its aliases lead to, is gone. Aliases form no cycle, so the walk ends
// within the store's capacity.
const muiTokenValue* muiResolveToken(const muiTokenStore* store, muiTokenId id);

#endif // MAUL_UI_SRC_TOKEN_STORE_H
