// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus (record mui-0007): what pointer input and edits tell it.

#ifndef MAUL_UI_SRC_FOCUS_H
#define MAUL_UI_SRC_FOCUS_H

#include "context.h"

#include <stdint.h>

// Whether a node's mode, at least least (never mui_focusNone), and the
// host's states let it take focus; a modal layer covering it is apart.
bool muiFocusTakes(const muiContext* context, uint32_t slot, muiFocusMode least);

// Whether a node is out of focus's reach: it or a node above it exits, or
// a modal layer above whatever layer holds it covers it, one in the
// node's tree, met from the top before a layer that holds it.
bool muiFocusIsCovered(const muiContext* context, uint32_t slot);

// The node after at in tree order within scope, passing over the layers
// in it, or 0.
uint32_t muiFocusFollowing(const muiTree* tree, uint32_t scope, uint32_t at);

// Where a player navigates under root: the layer holding its focus,
// without the layers in it, or, with no focus there or a covered one,
// the top modal layer under root or root. focusOut receives the focus
// when it is in the scope, else 0.
uint32_t muiFocusScope(const muiContext* context, uint32_t root, uint8_t player,
                       uint32_t* focusOut);

// Moves a player's focus to slot (0 for none), shown or not.
void muiFocusAssign(muiContext* context, uint8_t player, uint32_t slot, bool shown);

// Moves a player's focus to slot by navigation: shown, and so is code's
// next focus.
void muiFocusNavigate(muiContext* context, uint8_t player, uint32_t slot);

// Focuses, for a player, the nearest node from slot up that takes focus,
// as a pointer press does; over nothing that does (slot 0 included), the
// player's focus goes.
void muiFocusPress(muiContext* context, uint8_t player, uint32_t slot);

// Lets the focus of nodes that no longer take it go: after a node's
// style or interaction values changed. Only a focused node has any.
void muiFocusRecheck(muiContext* context, uint32_t slot);

static inline void muiNoteFocus(muiContext* context, uint32_t slot)
{
    if (context->style.nodes[slot - 1].focusedBy != 0)
    {
        muiFocusRecheck(context, slot);
    }
}

void muiFocusForgetDestroyed(muiContext* context);

// Lets the focus of destroyed nodes go, after a destruction; only a
// player that focuses a node has any.
static inline void muiNoteDestroyed(muiContext* context)
{
    if (context->focus.holders != 0)
    {
        muiFocusForgetDestroyed(context);
    }
}

#endif // MAUL_UI_SRC_FOCUS_H
