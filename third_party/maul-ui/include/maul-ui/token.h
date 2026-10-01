// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tokens: typed values that class variants name in place of a property's
// value, so that a theme is a change of token values (record mui-0004).
// A token holds a literal of its type, or an alias to another token of
// that type; aliases never form a cycle. Changing a token restyles every
// node, as a class edit does, and named transitions move the change.

#ifndef MAUL_UI_TOKEN_H
#define MAUL_UI_TOKEN_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A token, in the shape of every id (family record 0016).
    typedef struct muiTokenId
    {
        uint32_t index1;
        uint32_t generation;
    } muiTokenId;

    // What a token holds, and so the properties it can feed.
    typedef uint8_t muiTokenType;

    enum
    {
        // A muiColor: background, border, image tint colors.
        mui_tokenColor = 1,
        // A float: gaps, grow and shrink, margins, borders, paddings,
        // anchors, aspect ratio, opacity.
        mui_tokenNumber = 2,
        // A muiDimension: sizes, basis, insets, corner radii.
        mui_tokenDimension = 3,
        mui_tokenShadow = 4,
        mui_tokenGradient = 5,
    };

    // A token's value: its type, and the member of that type.
    typedef struct muiTokenValue
    {
        muiTokenType type;
        union
        {
            muiColor color;
            float number;
            muiDimension dimension;
            muiShadow shadow;
            muiGradient gradient;
        };
    } muiTokenValue;

    /// Creates a token holding a value; its type is the value's, for good.
    ///
    /// @param context     The context.
    /// @param value       The value: a known type and a member valid for it
    ///                    (components of a color from 0 to 1, a finite
    ///                    number, a dimension of a known kind with finite
    ///                    parts, a shadow and a gradient as
    ///                    muiStyle_SetVisualValues takes them).
    /// @param tokenIdOut  Receives the token; set to the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         value outside the above or a call from a measure or paint function;
    ///         `mui_errorCapacity` when the context's token limit is
    ///         reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateToken(muiContext* context, const muiTokenValue* value,
                                                   muiTokenId* tokenIdOut);

    /// Destroys a token. Variants that name it, and tokens that alias it,
    /// then give no value through it, and every node is restyled.
    ///
    /// @param context  The context.
    /// @param tokenId  The token.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function; `mui_errorStale`
    ///         for an id whose token is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyToken(muiContext* context, muiTokenId tokenId);

    /// Gives a token a literal value, ending any alias, and restyles every
    /// node.
    ///
    /// @param context  The context.
    /// @param tokenId  The token.
    /// @param value    The value, of the token's type, as muiCreateToken
    ///                 takes it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, a value of another type or outside muiCreateToken's
    ///         or a call from a measure or paint function; `mui_errorStale` for an
    ///         id whose token is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetTokenValue(muiContext* context, muiTokenId tokenId,
                                                     const muiTokenValue* value);

    /// Makes a token an alias of another of its type, so that it gives
    /// that token's value, and restyles every node.
    ///
    /// @param context  The context.
    /// @param tokenId  The token.
    /// @param target   The token it gives the value of.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, a null
    ///         id, a target of another type, an alias that would lead back
    ///         to the token or a call from a measure or paint function;
    ///         `mui_errorStale` for a token or a target that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiSetTokenAlias(muiContext* context, muiTokenId tokenId,
                                                     muiTokenId target);

    /// Reads a token's value, through its aliases, and what it aliases.
    ///
    /// @param context     The context.
    /// @param tokenId     The token.
    /// @param valueOut    Receives the value, with the token's type; its
    ///                    member is zero when the result is `mui_empty`.
    /// @param aliasOut    Receives the token it aliases, or the null id for
    ///                    a literal; may be NULL.
    /// @return `mui_success`; `mui_empty` when an alias leads to a token
    ///         that is gone; `mui_errorInvalid` for a NULL context or
    ///         value, or the null id; `mui_errorStale` for an id whose
    ///         token is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiGetTokenValue(const muiContext* context, muiTokenId tokenId,
                                                     muiTokenValue* valueOut, muiTokenId* aliasOut);

    /// Names a token for a property in one variant of a class, in place
    /// of a value: resolution reads the token there. A value the property
    /// does not allow, or no value, leaves that layer silent for the
    /// property. Setting a value for the property with
    /// muiStyle_SetLayoutValues or muiStyle_SetVisualValues ends the name,
    /// and this ends the value; the null id ends the name alone.
    ///
    /// @param context   The context.
    /// @param styleId   The class.
    /// @param variant   The variant.
    /// @param property  The property; one that takes a token of a type
    ///                  above, which a condition of the variant does not
    ///                  read.
    /// @param tokenId   The token, of the property's type, or the null id.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null class id, an unknown variant or property, a property
    ///         of no token type or another type than the token's, one the
    ///         variant's condition reads, or a call from a measure
    ///         function; `mui_errorStale` for a class or a token that is
    ///         gone; `mui_errorCapacity` when the context's limit of token
    ///         names, or of property sets for a variant with nothing set,
    ///         is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_SetToken(muiContext* context, muiStyleId styleId,
                                                      muiVariant variant, muiProperty property,
                                                      muiTokenId tokenId);

    /// Reads the token one variant of a class names for a property.
    ///
    /// @param context     The context.
    /// @param styleId     The class.
    /// @param variant     The variant.
    /// @param property    The property.
    /// @param tokenIdOut  Receives the token; the null id for none.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, the
    ///         null id, an unknown variant or property; `mui_errorStale`
    ///         for an id whose class is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiStyle_GetToken(const muiContext* context, muiStyleId styleId,
                                                      muiVariant variant, muiProperty property,
                                                      muiTokenId* tokenIdOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_TOKEN_H
