// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus (record mui-0007): the node each player's keys and gamepad go to,
// one per player slot for local multiplayer, moved by code, pointer
// presses and sequential navigation, and shown as the input that moved
// it calls for.

#ifndef MAUL_UI_FOCUS_H
#define MAUL_UI_FOCUS_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        // The player slots a context keeps a focus for.
        MUI_MAX_PLAYERS = 8
    };

    // What moved a player's focus, which decides whether it is shown.
    typedef uint8_t muiFocusCause;

    enum
    {
        // Code: shown unless the player's last focus move was by a
        // pointer, as the web's script focus follows the last input.
        mui_focusByCode = 0,
        // A pointer press: not shown.
        mui_focusByPointer = 1,
        // Keys or a gamepad navigating: shown.
        mui_focusByNavigation = 2,
    };

    /// Moves a player's focus to a node, or takes it away. A node takes
    /// focus when its focus mode (as its last style resolution or direct
    /// write left it) is not mui_focusNone, it is neither disabled nor
    /// exiting, and no modal layer covers it. The node gets
    /// mui_stateFocused, and mui_stateFocusVisible when the focus is
    /// shown; a focus that moves posts mui_notificationFocusLost for the
    /// node it leaves and mui_notificationFocusGained for the one it
    /// reaches, with the player as the count.
    ///
    /// @param context  The context.
    /// @param player   The player, below MUI_MAX_PLAYERS.
    /// @param nodeId   The node; the null id takes the focus away.
    /// @param cause    What moved it.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, a
    ///         player or cause outside the above, a node that takes no
    ///         focus or a call from a measure or paint function, which
    ///         changes nothing; `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiFocus_Set(muiContext* context, uint8_t player,
                                                 muiNodeId nodeId, muiFocusCause cause);

    /// Returns the node a player focuses.
    ///
    /// @param context  The context.
    /// @param player   The player.
    /// @return The node; the null id for none, a NULL context or a player
    ///         past MUI_MAX_PLAYERS.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiFocus_Get(const muiContext* context, uint8_t player);

    /// Moves a player's focus to the next or previous node in sequential
    /// order (Tab and Shift+Tab), shown. The order runs over the nodes
    /// whose focus mode is mui_focusAll in the layer that holds the focus:
    /// a layer's subtree, or the root's subtree, without the layers in it;
    /// those with a tab order of 1 to 255 first, ascending, then the rest,
    /// ties in tree order; it wraps. With no focus under the root, or one
    /// a modal layer covers, it starts in the top modal layer under the
    /// root, or else the root's own content.
    ///
    /// @param context   The context.
    /// @param rootId    The root of the subtree the player navigates.
    /// @param player    The player.
    /// @param backward  Whether to go to the previous node.
    /// @return `mui_success`; `mui_empty` when no node there takes focus,
    ///         which leaves it; `mui_errorInvalid` for a NULL context, the
    ///         null id, a player past MUI_MAX_PLAYERS or a call from a
    ///         measure or paint function; `mui_errorStale` for a root that
    ///         is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiFocus_Move(muiContext* context, muiNodeId rootId,
                                                  uint8_t player, bool backward);

    // A direction on the screen.
    typedef uint8_t muiDirection;

    enum
    {
        mui_directionUp = 0,
        mui_directionDown = 1,
        mui_directionLeft = 2,
        mui_directionRight = 3,
    };

    /// Moves a player's focus toward a direction on the screen (arrow keys,
    /// a gamepad's pad or stick), shown. A link the focused node has for
    /// the direction (muiNode_SetNeighbor) to a node that takes focus
    /// wins; a link to the node itself stops the move. Otherwise the
    /// nearest node in the direction is found as Android's focus search
    /// finds it, among the nodes sequential navigation reaches in the same
    /// layer: nodes overlapping the focus across the direction first, then
    /// the least of 13 times the square of the gap along the direction
    /// plus the square of the distance between centers across it, ties to
    /// the earlier in tree order. Boxes are as the last muiComputeLayout
    /// left them. With no focus under the root, or one a modal layer
    /// covers, it moves as muiFocus_Move does forward.
    ///
    /// @param context    The context.
    /// @param rootId     The root of the subtree the player navigates.
    /// @param player     The player.
    /// @param direction  The direction.
    /// @return `mui_success`; `mui_empty` when nothing lies that way, which
    ///         leaves the focus; `mui_errorInvalid` for a NULL context, the
    ///         null id, a player past MUI_MAX_PLAYERS, an unknown direction
    ///         or a call from a measure or paint function;
    ///         `mui_errorStale` for a root that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiFocus_MoveToward(muiContext* context, muiNodeId rootId,
                                                        uint8_t player, muiDirection direction);

    /// Links a node to the node directional navigation moves to from it
    /// in a direction, over what geometry would find; the node itself
    /// stops movement that way. Links to nodes that do not take focus
    /// when the move is made leave it to geometry.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param direction  The direction.
    /// @param targetId   The node to move to; the null id removes the link.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id as the node, an unknown direction or a call from a
    ///         measure or paint function; `mui_errorStale` for a node or
    ///         target that is gone; `mui_errorCapacity` past the context's
    ///         neighbors limit.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_SetNeighbor(muiContext* context, muiNodeId nodeId,
                                                        muiDirection direction, muiNodeId targetId);

    /// Returns the node a node links to in a direction.
    ///
    /// @param context    The context.
    /// @param nodeId     The node.
    /// @param direction  The direction.
    /// @return The target, which may be gone since; the null id for no
    ///         link, a NULL context, a node that is gone or an unknown
    ///         direction.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetNeighbor(const muiContext* context, muiNodeId nodeId,
                                          muiDirection direction);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_FOCUS_H
