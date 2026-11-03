// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's themes: each a table from token slots to override records,
// so that finding a theme's override of a token is one read.

#ifndef MAUL_UI_SRC_THEME_STORE_H
#define MAUL_UI_SRC_THEME_STORE_H

#include "pool.h"
#include "token_store.h"

#include "maul-ui/theme.h"

// A theme's value for a token: a literal, or an alias when alias names a
// token.
typedef struct muiThemeOverride
{
    muiTokenId token;
    muiTokenId alias;
    muiTokenValue value;
} muiThemeOverride;

typedef struct muiThemeStore
{
    muiPool pool;
    muiPool overridePool;
    muiThemeOverride* overrides;
    // Per theme, one entry per token slot: the override record, or 0.
    uint32_t* tables;
    uint32_t tokenCapacity;
} muiThemeStore;

// The themes a node reads, nearest first, as theme slots.
typedef struct muiThemeScope
{
    uint32_t themes[MUI_MAX_THEME_DEPTH];
    uint32_t count;
} muiThemeScope;

// The entry of a theme's table for a token slot.
uint32_t* muiThemeEntry(const muiThemeStore* store, uint32_t theme, uint32_t tokenSlot);

// A theme's override of a live token, or NULL.
const muiThemeOverride* muiFindOverride(const muiThemeStore* store, uint32_t theme,
                                        muiTokenId token);

// Whether the aliases from target lead to token, where each token may
// follow the context's alias or any live theme's: an alias from token to
// target would close a cycle under some nesting of themes. Marks the
// tokens it reaches in their records.
bool muiAliasLeadsTo(muiTokenStore* tokens, const muiThemeStore* themes, muiTokenId target,
                     muiTokenId token);

// A token's value as a scope reads it: each step takes the nearest
// override, or the context's token, and follows an alias on. NULL when a
// token on the way is gone. The aliases of the context and every theme
// together form no cycle (muiAliasLeadsTo), so the walk ends within as
// many steps as there are tokens.
const muiTokenValue* muiResolveTokenIn(const muiTokenStore* tokens, const muiThemeStore* themes,
                                       const muiThemeScope* scope, muiTokenId id);

#endif // MAUL_UI_SRC_THEME_STORE_H
