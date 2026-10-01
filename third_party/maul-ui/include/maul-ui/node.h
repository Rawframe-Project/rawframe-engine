// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Nodes: the retained tree a host builds and edits through ids. A node
// without a parent is a root; each root is a separate tree.

#ifndef MAUL_UI_NODE_H
#define MAUL_UI_NODE_H

#include "maul-ui/base.h"
#include "maul-ui/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // How a node is made. Build it with muiDefaultNodeDef.
    typedef struct muiNodeDef
    {
        uint32_t cookie;
        // A value the host chooses to find its own object from the node,
        // returned by muiNode_GetHostKey. The library never reads it.
        uint64_t hostKey;
    } muiNodeDef;

    /// Returns the default node def: host key 0.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiNodeDef muiDefaultNodeDef(void);

    /// Creates a node, a root until it is inserted into a parent.
    ///
    /// @param context    The context.
    /// @param def        The node: a valid cookie.
    /// @param nodeIdOut  Receives the node's id; the null id on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument or a
    ///         bad cookie; `mui_errorCapacity` when the context's node limit
    ///         is reached.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiCreateNode(muiContext* context, const muiNodeDef* def,
                                                  muiNodeId* nodeIdOut);

    /// Destroys a node and its whole subtree, detaching it from its parent
    /// first. The ids of every destroyed node become stale.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiDestroyNode(muiContext* context, muiNodeId nodeId);

    /// Returns whether an id names a live node. A stale id is not misuse.
    ///
    /// @param context  The context.
    /// @param nodeId   Any id.
    /// @return True for a live node; false otherwise and for a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API bool muiNode_IsValid(const muiContext* context, muiNodeId nodeId);

    /// Inserts a root into a parent, before one of the parent's children or
    /// last.
    ///
    /// @param context   The context.
    /// @param parentId  The parent.
    /// @param childId   The child: a root that is not the parent or one of
    ///                  its ancestors.
    /// @param beforeId  A child of the parent to insert before, or the null
    ///                  id to append.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context, a null
    ///         parent or child, a child that has a parent, a child that is
    ///         the parent or one of its ancestors, or a before id that is not
    ///         the parent's child; `mui_errorStale` for an id whose node is
    ///         gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_InsertChild(muiContext* context, muiNodeId parentId,
                                                        muiNodeId childId, muiNodeId beforeId);

    /// Detaches a node from its parent, making it a root with its subtree.
    /// A root stays as it is.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL context or the
    ///         null id; `mui_errorStale` for an id whose node is gone.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiNode_Detach(muiContext* context, muiNodeId nodeId);

    /// Returns a node's parent.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The parent; the null id for a root, a stale id or a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetParent(const muiContext* context, muiNodeId nodeId);

    /// Returns a node's first child.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The first child; the null id for none, a stale id or a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetFirstChild(const muiContext* context, muiNodeId nodeId);

    /// Returns a node's last child.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The last child; the null id for none, a stale id or a NULL
    ///         context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetLastChild(const muiContext* context, muiNodeId nodeId);

    /// Returns the child after a node in its parent's order.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The next sibling; the null id for the last child, a root, a
    ///         stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetNextSibling(const muiContext* context, muiNodeId nodeId);

    /// Returns the child before a node in its parent's order.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The previous sibling; the null id for the first child, a
    ///         root, a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API muiNodeId muiNode_GetPreviousSibling(const muiContext* context, muiNodeId nodeId);

    /// Returns how many children a node has.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The count; 0 for a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API uint32_t muiNode_GetChildCount(const muiContext* context, muiNodeId nodeId);

    /// Returns the host key a node was created with.
    ///
    /// @param context  The context.
    /// @param nodeId   The node.
    /// @return The key; 0 for a stale id or a NULL context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MUI_API uint64_t muiNode_GetHostKey(const muiContext* context, muiNodeId nodeId);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_NODE_H
