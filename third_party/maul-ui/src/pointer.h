// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pointer input (record mui-0007): what tree edits tell it, so the hover
// and press counts along pointers' chains follow the tree.

#ifndef MAUL_UI_SRC_POINTER_H
#define MAUL_UI_SRC_POINTER_H

#include "context.h"

#include <stdint.h>

// The pointers whose hover or press an edit of a subtree moves: bit i
// for the pointer at i, hover in the low word, press in the high.
typedef uint64_t muiPointersTaken;

muiPointersTaken muiTakePointersOff(muiContext* context, uint32_t node);

// Ends every drag going, cancelled, and its capture (Escape's default);
// whether there was one.
bool muiCancelDrags(muiContext* context);
void muiPutPointersOn(muiContext* context, muiPointersTaken taken);

// Takes off the counts of the pointers whose hovered or pressed node is
// in the subtree of node, before an edit moves or destroys it; only a
// node some pointer's chain holds has any.
static inline muiPointersTaken muiPointersOff(muiContext* context, uint32_t node)
{
    const muiNodeStyle* style = &context->style.nodes[node - 1];
    return (style->hovers | style->presses) == 0 ? 0 : muiTakePointersOff(context, node);
}

// Puts back the counts muiPointersOff took, after the edit; pointers
// whose node is gone let it go, a captured one with a record.
static inline void muiPointersOn(muiContext* context, muiPointersTaken taken)
{
    if (taken != 0)
    {
        muiPutPointersOn(context, taken);
    }
}

#endif // MAUL_UI_SRC_POINTER_H
