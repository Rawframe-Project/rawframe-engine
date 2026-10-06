// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exit transitions (record mui-0007): a node the host removes plays its
// way out before it goes. Beginning an exit gives the node the exiting
// state, so its classes' exiting variants and their transitions apply;
// the node and its subtree leave hit testing, focus and navigation at
// once, and focus inside it is given up. When no transition runs in the
// subtree any more, at a layout, mui_notificationExitFinished reports it,
// once; the library never destroys the node. The host destroys it then,
// or earlier, which ends the exit, or cancels the exit to bring it back.

#ifndef MAUL_UI_EXIT_H
#define MAUL_UI_EXIT_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/node.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Begins a node's exit; nothing for a node already exiting.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorCapacity` when the context holds
    ///         its limit of exits; `mui_errorInvalid` for a NULL context,
    ///         the null id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_BeginExit(muiContext* context, muiNodeId nodeId);

    /// Cancels a node's exit: the exiting state goes, and the node and its
    /// subtree take input again; nothing for a node not exiting.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, the
    ///         null id or a call from a measure or paint function;
    ///         `mui_errorStale` for a node that is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_CancelExit(muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_EXIT_H
