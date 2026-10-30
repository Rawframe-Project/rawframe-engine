// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the clip's builder (src/clip.c) and its reads (src/clip_read.c)
// share: finding a clip by id and walking its nodes.

#ifndef MAUL_TWEEN_SRC_CLIP_INTERNAL_H
#define MAUL_TWEEN_SRC_CLIP_INTERNAL_H

#include "context_internal.h"

static inline mtwnClipSlot* mtwnFindClip(const mtwnContext* context, mtwnClipId clip)
{
    return (mtwnClipSlot*)mtwnPoolFind(&context->clips, clip.index1, clip.generation);
}

static inline const mtwnClipNode* mtwnClipNodeAt(const mtwnClipSlot* clip, uint32_t index1)
{
    return &clip->nodes[index1 - 1];
}

static inline bool mtwnClipIsTimeline(const mtwnClipSlot* clip, uint32_t index1)
{
    return index1 >= 1 && index1 <= clip->nodeCount &&
           mtwnClipNodeAt(clip, index1)->kind == mtwn_nodeTimeline;
}

// The label of a parent with a key, 0 when none.
static inline uint32_t mtwnClipLabelIn(const mtwnClipSlot* clip, uint32_t parent, uint32_t key)
{
    for (uint32_t child = mtwnClipNodeAt(clip, parent)->firstChild; child != 0;
         child = mtwnClipNodeAt(clip, child)->nextSibling)
    {
        const mtwnClipNode* node = mtwnClipNodeAt(clip, child);
        if (node->kind == mtwn_nodeLabel && node->key == key)
        {
            return child;
        }
    }
    return 0;
}

// A timeline's timing with its duration resolved.
static inline mtwnTiming mtwnClipTimelineTiming(const mtwnClipNode* node, double childrenEnd)
{
    mtwnTiming timing = node->timing;
    if (node->autoDuration)
    {
        timing.duration = childrenEnd;
    }
    return timing;
}

#endif // MAUL_TWEEN_SRC_CLIP_INTERNAL_H
