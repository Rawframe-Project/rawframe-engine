// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Themes: sets of token values that override the context's tokens for
// the subtree a theme is set on (record mui-0004). A node reads a token
// from the nearest theme above it, itself included, that overrides it,
// then the next one out, then the context; an alias read in a subtree
// is read there too. Editing a theme restyles every node; setting one on
// a node restyles the nodes whose themes it changes.

#ifndef MAUL_UI_THEME_H
#define MAUL_UI_THEME_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"
#include "maul-ui/token.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        // The nested themes a node reads, nearest first; themes further
        // out than these are passed by.
        MUI_MAX_THEME_DEPTH = 8
    };

    // A theme, in the shape of every id (family record 0016).
    typedef struct muiThemeId
    {
        uint32_t index1;
        uint32_t generation;
    } muiThemeId;

    // How a theme is made. Build it with muiDefaultThemeDef.
    typedef struct muiThemeDef
    {
        uint32_t cookie;
    } muiThemeDef;

    /// Returns the default theme def.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiThemeDef muiDefaultThemeDef(void);

    /// Creates a theme that overrides no token.
    ///
    /// @param context     The context.
    /// @param def         The theme: a valid cookie.
    /// @param themeIdOut  Receives the theme; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad
    ///         cookie or a call from a measure or paint function;
    ///         `mui_errorCapacity` when the context's theme limit is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateTheme(muiContext* context, const muiThemeDef* def,
                                                   muiThemeId* themeIdOut);

    /// Destroys a theme and its overrides. Nodes it was set on read
    /// through no theme of their own, and every node is restyled.
    ///
    /// @param context  The context.
    /// @param themeId  The theme.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose theme is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyTheme(muiContext* context, muiThemeId themeId);

    /// Overrides a token in a theme with a literal value.
    ///
    /// @param context  The context.
    /// @param themeId  The theme.
    /// @param tokenId  The token.
    /// @param value    The value, of the token's type, as muiCreateToken
    ///                 takes it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         null id, a value of another type or outside muiCreateToken's
    ///         or a call from a measure or paint function; `mui_errorStale` for a
    ///         theme or a token that is gone; `mui_errorCapacity` when the
    ///         token was not overridden and the context's limit of theme
    ///         overrides is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTheme_SetTokenValue(muiContext* context, muiThemeId themeId,
                                                           muiTokenId tokenId,
                                                           const muiTokenValue* value);

    /// Overrides a token in a theme with an alias of another token of its
    /// type, read in the subtree that reads the theme.
    ///
    /// @param context  The context.
    /// @param themeId  The theme.
    /// @param tokenId  The token.
    /// @param target   The token it gives the value of.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, a null
    ///         id, a target of another type, an alias that would lead back
    ///         to the token through this theme and the context, or a call
    ///         from a measure or paint function; `mui_errorStale` for a theme, token
    ///         or target that is gone; `mui_errorCapacity` as
    ///         muiTheme_SetTokenValue.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTheme_SetTokenAlias(muiContext* context, muiThemeId themeId,
                                                           muiTokenId tokenId, muiTokenId target);

    /// Ends a theme's override of a token.
    ///
    /// @param context  The context.
    /// @param themeId  The theme.
    /// @param tokenId  The token.
    /// @return `mui_success`, also when the theme did not override it;
    ///         `mui_errorInvalid` for a NULL context, a null id or a call
    ///         from a measure or paint function; `mui_errorStale` for a theme or a
    ///         token that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTheme_ResetToken(muiContext* context, muiThemeId themeId,
                                                        muiTokenId tokenId);

    /// Reads a theme's override of a token.
    ///
    /// @param context   The context.
    /// @param themeId   The theme.
    /// @param tokenId   The token.
    /// @param valueOut  Receives the literal, with the token's type; its
    ///                  member is zero for an alias or no override.
    /// @param aliasOut  Receives the token aliased, or the null id; may be
    ///                  NULL.
    /// @return `mui_success`; `mui_empty` when the theme does not override
    ///         the token; `mui_errorInvalid` for a NULL context or value, or
    ///         a null id; `mui_errorStale` for a theme or a token that is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiTheme_GetToken(const muiContext* context, muiThemeId themeId,
                                                      muiTokenId tokenId, muiTokenValue* valueOut,
                                                      muiTokenId* aliasOut);

    /// Sets the theme a node and its subtree read; the null id sets none.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @param themeId  The theme, or the null id.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null node id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node or a theme that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetTheme(muiContext* context, muiNodeId nodeId,
                                                     muiThemeId themeId);

    /// Reads the theme set on a node.
    ///
    /// @param context     The context.
    /// @param nodeId      The node.
    /// @param themeIdOut  Receives the theme set, or the null id.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetTheme(const muiContext* context, muiNodeId nodeId,
                                                     muiThemeId* themeIdOut);

    /// Reads a token's value as a node reads it, through the themes above
    /// it as of the last muiComputeLayout that styled it.
    ///
    /// @param context   The context.
    /// @param nodeId    The node.
    /// @param tokenId   The token.
    /// @param valueOut  Receives the value, with the token's type; its
    ///                  member is zero when the result is `mui_empty`.
    /// @return `mui_success`; `mui_empty` when the token gives the node no
    ///         value (an alias to a token that is gone, or a cycle that
    ///         only nested themes make); `mui_errorInvalid` for a NULL
    ///         context or value, or a null id; `mui_errorStale` for a node
    ///         or a token that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_GetTokenValue(const muiContext* context,
                                                          muiNodeId nodeId, muiTokenId tokenId,
                                                          muiTokenValue* valueOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_THEME_H
