// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Clips' reads: a sealed clip's tracks, nodes, labels and duration, each
// call in the library's floating-point environment.

#include "clip_internal.h"
#include "context_internal.h"
#include "fpenv.h"

#include <string.h>

// Counts a refusal against a live context as misuse.
static mtwnResult Misuse(mtwnContext* context, mtwnResult result)
{
    mtwnCountMisuse(context);
    return result;
}

// The sealed clip an id names, with the refusal counted.
static mtwnResult Sealed(mtwnContext* context, mtwnClipId clip, mtwnClipSlot** slotOut)
{
    *slotOut = mtwnFindClip(context, clip);
    if (*slotOut == NULL)
    {
        return Misuse(context, mtwn_errorStale);
    }
    if (!(*slotOut)->sealed)
    {
        return Misuse(context, mtwn_errorState);
    }
    // Its nodes hold the root, a timeline, and at most nodeCapacity more
    // (clip.c's HasRoom), which every read indexes within.
    MTWN_ASSERT((*slotOut)->nodeCount >= 1 &&
                (*slotOut)->nodeCount <= (*slotOut)->def.nodeCapacity + 1);
    MTWN_ASSERT((*slotOut)->nodes[0].kind == mtwn_nodeTimeline);
    return mtwn_success;
}

// Whether a node's parent is as clip.c's Attach puts it: none for the
// root, otherwise a timeline before it. Only an assertion calls it.
[[maybe_unused]] static inline bool ParentBefore(const mtwnClipSlot* slot, uint32_t node)
{
    uint32_t parent = mtwnClipNodeAt(slot, node)->parent;
    return node == MTWN_ROOT_NODE ? parent == 0 : mtwnClipIsTimeline(slot, parent) && parent < node;
}

static mtwnResult RunClipGetTrack(mtwnContext* context, mtwnClipId clip, mtwnNode node,
                                  mtwnClipTrack* trackOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (trackOut == NULL)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    memset(trackOut, 0, sizeof *trackOut);
    mtwnClipSlot* slot = NULL;
    mtwnResult result = Sealed(context, clip, &slot);
    if (result != mtwn_success)
    {
        return result;
    }
    if (node < 1 || node > slot->nodeCount || slot->nodes[node - 1].kind != mtwn_nodeTrack)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    const mtwnClipNode* n = &slot->nodes[node - 1];
    MTWN_ASSERT(ParentBefore(slot, node));
    // The slots its tracks use are below slotCount (clip.c's
    // AddTrackWorker raises it to each).
    MTWN_ASSERT(n->slot < slot->slotCount);
    *trackOut = (mtwnClipTrack){n->track, n->timing, n->start, n->parent, n->slot};
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnClip_GetTrackWork(mtwnContext* context, mtwnClipId clip,
                                                      mtwnNode node, mtwnClipTrack* trackOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (trackOut != NULL)
    {
        memset(trackOut, 0, sizeof *trackOut);
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipGetTrack(context, clip, node, trackOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_GetTrack(mtwnContext* context, mtwnClipId clip, mtwnNode node,
                             mtwnClipTrack* trackOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_GetTrackWork(context, clip, node, trackOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunClipGetNode(mtwnContext* context, mtwnClipId clip, mtwnNode node,
                                 mtwnClipNodeInfo* infoOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (infoOut == NULL)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    memset(infoOut, 0, sizeof *infoOut);
    mtwnClipSlot* slot = NULL;
    mtwnResult result = Sealed(context, clip, &slot);
    if (result != mtwn_success)
    {
        return result;
    }
    if (node < 1 || node > slot->nodeCount)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    const mtwnClipNode* n = mtwnClipNodeAt(slot, node);
    MTWN_ASSERT(ParentBefore(slot, node));
    // A timeline's children end starts at 0 and only rises to an end
    // (clip.c's Raise), so it is never negative or NaN.
    MTWN_ASSERT(n->kind != mtwn_nodeTimeline || n->childrenEnd >= 0.0);
    infoOut->timing =
        n->kind == mtwn_nodeTimeline ? mtwnClipTimelineTiming(n, n->childrenEnd) : n->timing;
    infoOut->start = n->start;
    infoOut->end = node == MTWN_ROOT_NODE ? n->childrenEnd : n->end;
    infoOut->parent = n->parent;
    infoOut->slot = n->kind == mtwn_nodeTrack ? n->slot : 0;
    infoOut->key = n->key;
    infoOut->kind = n->kind;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnClip_GetNodeWork(mtwnContext* context, mtwnClipId clip,
                                                     mtwnNode node, mtwnClipNodeInfo* infoOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (infoOut != NULL)
    {
        memset(infoOut, 0, sizeof *infoOut);
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipGetNode(context, clip, node, infoOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_GetNode(mtwnContext* context, mtwnClipId clip, mtwnNode node,
                            mtwnClipNodeInfo* infoOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_GetNodeWork(context, clip, node, infoOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunClipFindLabel(mtwnContext* context, mtwnClipId clip, mtwnNode parent,
                                   uint32_t key, double* secondsOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (secondsOut == NULL)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    *secondsOut = 0.0;
    mtwnClipSlot* slot = NULL;
    mtwnResult result = Sealed(context, clip, &slot);
    if (result != mtwn_success)
    {
        return result;
    }
    uint32_t label = mtwnClipIsTimeline(slot, parent) ? mtwnClipLabelIn(slot, parent, key) : 0;
    if (label == 0)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    // The parent's children are its own nodes, within the clip.
    MTWN_ASSERT(label <= slot->nodeCount && mtwnClipNodeAt(slot, label)->parent == parent);
    *secondsOut = mtwnClipNodeAt(slot, label)->start;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnClip_FindLabelWork(mtwnContext* context, mtwnClipId clip,
                                                       mtwnNode parent, uint32_t key,
                                                       double* secondsOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (secondsOut != NULL)
    {
        *secondsOut = 0.0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipFindLabel(context, clip, parent, key, secondsOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_FindLabel(mtwnContext* context, mtwnClipId clip, mtwnNode parent, uint32_t key,
                              double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_FindLabelWork(context, clip, parent, key, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunClipGetDuration(mtwnContext* context, mtwnClipId clip, double* secondsOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (secondsOut == NULL)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    *secondsOut = 0.0;
    mtwnClipSlot* slot = NULL;
    mtwnResult result = Sealed(context, clip, &slot);
    if (result == mtwn_success)
    {
        // As every timeline's children end (clip.c's Raise).
        MTWN_ASSERT(slot->nodes[0].childrenEnd >= 0.0);
        *secondsOut = slot->nodes[0].childrenEnd;
    }
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnClip_GetDurationWork(mtwnContext* context, mtwnClipId clip,
                                                         double* secondsOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (secondsOut != NULL)
    {
        *secondsOut = 0.0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipGetDuration(context, clip, secondsOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_GetDuration(mtwnContext* context, mtwnClipId clip, double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_GetDurationWork(context, clip, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}
