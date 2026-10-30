// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The players' slots, transitions' and clips' alike:
// taken last in creation order, and the clips' players in a chain of
// their own. Apart from the clips' players' procedures, so that a host
// with no clip's player links none of them.

#include "context_internal.h"

uint32_t mtwnTakePlayer(mtwnContext* context, uint8_t type)
{
    uint32_t index1 = mtwnPoolAcquire(&context->players);
    if (index1 == 0)
    {
        return 0;
    }
    mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
    slot->type = type;

    slot->earlier = context->lastPlayer;
    if (context->lastPlayer != 0)
    {
        mtwnPlayerAt(context, context->lastPlayer)->later = index1;
    }
    else
    {
        context->firstPlayer = index1;
    }
    context->lastPlayer = index1;
    if (type == MTWN_PLAYER_CLIP)
    {
        slot->play.nextClip = context->firstClipPlayer;
        if (context->firstClipPlayer != 0)
        {
            mtwnPlayerAt(context, context->firstClipPlayer)->play.previousClip = index1;
        }
        context->firstClipPlayer = index1;
    }
    return index1;
}

void mtwnFreePlayer(mtwnContext* context, uint32_t player)
{
    mtwnPlayerSlot* slot = mtwnPlayerAt(context, player);
    if (slot->earlier != 0)
    {
        mtwnPlayerAt(context, slot->earlier)->later = slot->later;
    }
    else
    {
        context->firstPlayer = slot->later;
    }
    if (slot->later != 0)
    {
        mtwnPlayerAt(context, slot->later)->earlier = slot->earlier;
    }
    else
    {
        context->lastPlayer = slot->earlier;
    }
    if (slot->type == MTWN_PLAYER_CLIP)
    {
        const mtwnClipPlay* play = &slot->play;
        if (play->previousClip != 0)
        {
            mtwnPlayerAt(context, play->previousClip)->play.nextClip = play->nextClip;
        }
        else
        {
            context->firstClipPlayer = play->nextClip;
        }
        if (play->nextClip != 0)
        {
            mtwnPlayerAt(context, play->nextClip)->play.previousClip = play->previousClip;
        }
        // One writer fewer of each target still alive; a destroyed one's
        // slot was zeroed when taken again. Looked up here, not through the
        // clips' code, which transitions link without.
        for (uint32_t s = 0; s < play->bindingCount; ++s)
        {
            mtwnTargetId id = context->bindings.entries[play->bindingStart + s].target;
            mtwnTargetSlot* target =
                id.index1 != 0
                    ? (mtwnTargetSlot*)mtwnPoolFind(&context->targets, id.index1, id.generation)
                    : NULL;
            if (target != NULL)
            {
                mtwnAddWriter(context, target, false);
            }
        }
        if (slot->play.bindingCount > 0)
        {
            mtwnGiveRun(&context->bindings, slot->play.bindingStart, slot->play.bindingCount);
        }
        --mtwnClipAt(context, slot->play.clip.index1)->users;
    }
    mtwnPoolRelease(&context->players, player);
}
