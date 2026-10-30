// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Targets in the context's pool.
// Values are stored as given and
// checked by their bits, so these calls do no floating-point arithmetic
// and give the same result in any floating-point environment.

#include "context_internal.h"
#include "fpenv.h"

#include <string.h>

static bool IsKind(mtwnValueKind kind)
{
    return kind <= mtwn_valueScale;
}

static uint32_t Bits(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

// Finite: the exponent's bits are not all ones.
static bool IsFiniteBits(float value)
{
    return (Bits(value) & 0x7F800000u) != 0x7F800000u;
}

// Zero of either sign.
static bool IsZeroBits(float value)
{
    return (Bits(value) & 0x7FFFFFFFu) == 0;
}

bool mtwnAcceptValue(mtwnValueKind kind, mtwnVec4 value, mtwnVec4* out)
{
    float channels[4] = {value.x, value.y, value.z, value.w};
    uint8_t used = mtwnKindChannels(kind);
    bool zero = true;
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (i >= used)
        {
            channels[i] = 0.0f;
            continue;
        }
        if (!IsFiniteBits(channels[i]))
        {
            return false;
        }
        zero = zero && IsZeroBits(channels[i]);
    }
    if ((kind == mtwn_valueRotation && zero) || (kind == mtwn_valueScale && !(channels[0] > 0.0f)))
    {
        return false;
    }
    *out = (mtwnVec4){channels[0], channels[1], channels[2], channels[3]};
    return true;
}

// The live slot an id names, or NULL.
static mtwnTargetSlot* Find(const mtwnContext* context, mtwnTargetId target)
{
    return (mtwnTargetSlot*)mtwnPoolFind(&context->targets, target.index1, target.generation);
}

static mtwnResult RunCreateTarget(mtwnContext* context, mtwnValueKind kind, mtwnVec4 value,
                                  mtwnTargetId* targetOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnVec4 accepted = {0};
    if (targetOut == NULL || !IsKind(kind) || !mtwnAcceptValue(kind, value, &accepted))
    {
        if (targetOut != NULL)
        {
            *targetOut = (mtwnTargetId){0, 0};
        }
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    *targetOut = (mtwnTargetId){0, 0};
    uint32_t index1 = mtwnPoolAcquire(&context->targets);
    if (index1 == 0)
    {
        return mtwn_errorCapacity;
    }
    mtwnTargetSlot* slot = mtwnTargetAt(context, index1);
    slot->base = accepted;
    slot->value = accepted;
    slot->kind = kind;
    *targetOut = (mtwnTargetId){index1, slot->header.generation};
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnCreateTargetWork(mtwnContext* context, mtwnValueKind kind,
                                                     mtwnVec4 value, mtwnTargetId* targetOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (targetOut != NULL)
    {
        *targetOut = (mtwnTargetId){0};
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunCreateTarget(context, kind, value, targetOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnCreateTarget(mtwnContext* context, mtwnValueKind kind, mtwnVec4 value,
                            mtwnTargetId* targetOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnCreateTargetWork(context, kind, value, targetOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunDestroyTarget(mtwnContext* context, mtwnTargetId target)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnTargetSlot* slot = Find(context, target);
    if (slot == NULL)
    {
        mtwnCountMisuse(context);
        return mtwn_errorStale;
    }
    if (slot->header.link != 0)
    {
        mtwnEndTransition(context, slot->header.link);
    }
    MTWN_ASSERT(slot->header.link == 0);
    // A target on the list of changed targets leaves it. It is on it once:
    // a clip group flags the targets it changes in its own work, and the
    // update merges every one of them onto the list before it returns.
    if ((slot->header.flags & MTWN_TARGET_CHANGED) != 0)
    {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < context->changedCount; ++i)
        {
            if (context->changed[i] != target.index1)
            {
                context->changed[kept++] = context->changed[i];
            }
        }
        MTWN_ASSERT(kept + 1u == context->changedCount);
        context->changedCount = kept;
    }
    // Its transition ended above: layered only by its clips' players.
    MTWN_ASSERT(!mtwnLayered(slot) || context->layeredTargets > 0);
    context->layeredTargets -= mtwnLayered(slot) ? 1u : 0u;
    ++context->writersEpoch;
    mtwnPoolRelease(&context->targets, target.index1);
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnDestroyTargetWork(mtwnContext* context, mtwnTargetId target)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunDestroyTarget(context, target);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnDestroyTarget(mtwnContext* context, mtwnTargetId target)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnDestroyTargetWork(context, target);
    mtwnFloatEnvLeave(env);
    return result;
}

bool mtwnTarget_IsValid(const mtwnContext* context, mtwnTargetId target)
{
    return context != NULL && Find(context, target) != NULL;
}

// The live slot an id names; a stale id counts as misuse.
static mtwnResult Resolve(mtwnContext* context, mtwnTargetId target, mtwnTargetSlot** slotOut)
{
    *slotOut = Find(context, target);
    if (*slotOut == NULL)
    {
        mtwnCountMisuse(context);
        return mtwn_errorStale;
    }
    return mtwn_success;
}

static mtwnResult RunTargetSetBase(mtwnContext* context, mtwnTargetId target, mtwnVec4 value)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnTargetSlot* slot = NULL;
    mtwnResult resolved = Resolve(context, target, &slot);
    if (resolved != mtwn_success)
    {
        return resolved;
    }
    mtwnVec4 accepted = {0};
    if (!mtwnAcceptValue(slot->kind, value, &accepted))
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    slot->base = accepted;
    // A target no transition writes shows its base at once; a clip track's
    // velocity left lazy is the old value's, so it is kept first.
    if (slot->header.link == 0)
    {
        mtwnSettleVelocity(context, target.index1);
        slot->value = accepted;
    }
    mtwnMarkChanged(context, target.index1);
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnTarget_SetBaseWork(mtwnContext* context, mtwnTargetId target,
                                                       mtwnVec4 value)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunTargetSetBase(context, target, value);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnTarget_SetBase(mtwnContext* context, mtwnTargetId target, mtwnVec4 value)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnTarget_SetBaseWork(context, target, value);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunTargetGetValue(mtwnContext* context, mtwnTargetId target, mtwnVec4* valueOut)
{
    if (context == NULL || valueOut == NULL)
    {
        if (context != NULL)
        {
            mtwnCountMisuse(context);
        }
        return mtwn_errorInvalid;
    }
    *valueOut = (mtwnVec4){0};
    mtwnTargetSlot* slot = NULL;
    mtwnResult resolved = Resolve(context, target, &slot);
    if (resolved == mtwn_success)
    {
        *valueOut = slot->value;
    }
    return resolved;
}

static MTWN_NOINLINE mtwnResult mtwnTarget_GetValueWork(mtwnContext* context, mtwnTargetId target,
                                                        mtwnVec4* valueOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (valueOut != NULL)
    {
        *valueOut = (mtwnVec4){0};
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunTargetGetValue(context, target, valueOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnTarget_GetValue(mtwnContext* context, mtwnTargetId target, mtwnVec4* valueOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnTarget_GetValueWork(context, target, valueOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunTargetGetState(mtwnContext* context, mtwnTargetId target,
                                    mtwnTargetState* stateOut)
{
    if (context == NULL || stateOut == NULL)
    {
        if (context != NULL)
        {
            mtwnCountMisuse(context);
        }
        return mtwn_errorInvalid;
    }
    memset(stateOut, 0, sizeof *stateOut);
    mtwnTargetSlot* slot = NULL;
    mtwnResult resolved = Resolve(context, target, &slot);
    if (resolved == mtwn_success)
    {
        stateOut->base = slot->base;
        stateOut->value = slot->value;
        stateOut->velocity = mtwnTargetVelocity(context, target.index1);
        stateOut->kind = slot->kind;
    }
    return resolved;
}

static MTWN_NOINLINE mtwnResult mtwnTarget_GetStateWork(mtwnContext* context, mtwnTargetId target,
                                                        mtwnTargetState* stateOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (stateOut != NULL)
    {
        memset(stateOut, 0, sizeof *stateOut);
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunTargetGetState(context, target, stateOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnTarget_GetState(mtwnContext* context, mtwnTargetId target, mtwnTargetState* stateOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnTarget_GetStateWork(context, target, stateOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunReadTargets(mtwnContext* context, const mtwnTargetId* targets, uint32_t count,
                                 mtwnVec4* valuesOut)
{
    if (context == NULL || (count != 0 && (targets == NULL || valuesOut == NULL)))
    {
        if (context != NULL)
        {
            mtwnCountMisuse(context);
        }
        return mtwn_errorInvalid;
    }
    mtwnResult result = mtwn_success;
    for (uint32_t i = 0; i < count; ++i)
    {
        const mtwnTargetSlot* slot = Find(context, targets[i]);
        if (slot == NULL)
        {
            mtwnCountMisuse(context);
            result = mtwn_errorStale;
            valuesOut[i] = (mtwnVec4){0};
            continue;
        }
        valuesOut[i] = slot->value;
    }
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnContext_ReadTargetsWork(mtwnContext* context,
                                                            const mtwnTargetId* targets,
                                                            uint32_t count, mtwnVec4* valuesOut)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunReadTargets(context, targets, count, valuesOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_ReadTargets(mtwnContext* context, const mtwnTargetId* targets,
                                   uint32_t count, mtwnVec4* valuesOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnContext_ReadTargetsWork(context, targets, count, valuesOut);
    mtwnFloatEnvLeave(env);
    return result;
}

uint32_t mtwnCountLayered(const mtwnContext* context)
{
    uint32_t layered = 0;
    for (uint32_t index1 = 1; index1 <= context->targets.high; ++index1)
    {
        const mtwnTargetSlot* slot = mtwnTargetAt(context, index1);
        bool alive = (slot->header.flags & MTWN_SLOT_ALIVE) != 0;
        layered += alive && mtwnLayered(slot) ? 1u : 0u;
    }
    return layered;
}
