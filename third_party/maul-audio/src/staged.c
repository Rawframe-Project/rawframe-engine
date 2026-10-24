// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Two-stage convolution (staged.h). The tail's period runs with the
// input it gathers: tail block j - 1 is computed while block j comes in,
// in T / b steps at the head's block boundaries, step 0 at the period's
// start taking in the block and any waiting response; its output fills
// region (j - 1) mod 2, which the period after plays while region j mod
// 2 waits for block j.

#include "staged.h"

#include <math.h>
#include <string.h>

static size_t Up(size_t bytes)
{
    return (bytes + 63) & ~(size_t)63;
}

maudStagedPlan maudPlanStaged(uint32_t block, uint32_t frames)
{
    maudStagedPlan plan = {.block = block};
    double ideal = sqrt((double)frames * (double)block / 2.0);
    uint32_t tail = 2 * block;
    while (tail < 16384 && (double)tail * 1.4142135623730951 <= ideal)
    {
        tail *= 2;
    }
    if (frames <= 2 * tail - block)
    {
        plan.headPartitions = frames > block ? (frames + block - 1) / block : 1;
        plan.headFrames = frames;
        return plan;
    }
    plan.tailBlock = tail;
    plan.headFrames = 2 * tail - block;
    plan.headPartitions = 2 * tail / block - 1;
    plan.tailPartitions = (frames - plan.headFrames + tail - 1) / tail;
    return plan;
}

size_t maudStagedBytes(const maudStagedPlan* plan, uint32_t channels)
{
    size_t bytes = Up(maudPartitionedBytes(plan->block, plan->headPartitions, channels));
    if (plan->tailBlock != 0)
    {
        uint32_t t = plan->tailBlock;
        bytes += Up(maudPartitionedBytes(t, plan->tailPartitions, 1)) +
                 Up((size_t)t * sizeof(float)) + Up(2 * (size_t)channels * t * sizeof(float));
    }
    return bytes;
}

size_t maudStagedResponseFloats(const maudStagedPlan* plan, uint32_t channels)
{
    size_t floats = maudResponseFloats(plan->block, plan->headPartitions, channels);
    if (plan->tailBlock != 0)
    {
        floats += maudResponseFloats(plan->tailBlock, plan->tailPartitions, channels);
    }
    return floats;
}

size_t maudStagedScratchFloats(const maudStagedPlan* plan)
{
    uint32_t largest = plan->tailBlock > plan->block ? plan->tailBlock : plan->block;
    return 4 * (size_t)largest + 2;
}

void maudResetStaged(maudStaged* s)
{
    maudResetPartitioned(&s->head);
    if (s->plan.tailBlock != 0)
    {
        uint32_t t = s->plan.tailBlock;
        maudResetPartitioned(&s->tail);
        memset(s->tailInput, 0, (size_t)t * sizeof(float));
        memset(s->tailOutput, 0, 2 * (size_t)s->channels * t * sizeof(float));
    }
    s->position = 0;
    s->period = 0;
    // A change under way is finished at once: nothing of the old
    // response is left to fade from.
    s->fading = false;
    s->previous = nullptr;
    s->head.response = s->head.next;
}

void maudInitStaged(maudStaged* s, const maudStagedPlan* plan, uint32_t channels, void* memory)
{
    *s = (maudStaged){
        .plan = *plan,
        .channels = channels,
        .headFloats = maudResponseFloats(plan->block, plan->headPartitions, channels),
    };
    unsigned char* m = memory;
    maudInitPartitioned(&s->head, plan->block, plan->headPartitions, channels, m);
    m += Up(maudPartitionedBytes(plan->block, plan->headPartitions, channels));
    if (plan->tailBlock != 0)
    {
        uint32_t t = plan->tailBlock;
        // One channel's worth: the tail only lends its ring, transform
        // and scratch; each channel's partitions are found by index.
        maudInitPartitioned(&s->tail, t, plan->tailPartitions, 1, m);
        m += Up(maudPartitionedBytes(t, plan->tailPartitions, 1));
        s->tailInput = (float*)m;
        m += Up((size_t)t * sizeof(float));
        s->tailOutput = (float*)m;
    }
    maudResetStaged(s);
}

void maudCutStagedResponse(const maudStaged* s, const float* const* samples, uint32_t frames,
                           float* response, float* scratch)
{
    uint32_t head = frames < s->plan.headFrames ? frames : s->plan.headFrames;
    maudPartitionResponse(&s->head, samples, head, response, scratch);
    if (s->plan.tailBlock == 0)
    {
        return;
    }
    uint32_t rest = frames - head;
    size_t floats = maudResponseFloats(s->plan.tailBlock, s->plan.tailPartitions, 1);
    for (uint32_t c = 0; c < s->channels; ++c)
    {
        const float* from = samples[c] + head;
        maudPartitionResponse(&s->tail, &from, rest, response + s->headFloats + c * floats,
                              scratch);
    }
}

void maudSetStagedResponse(maudStaged* s, const float* response)
{
    s->pending = response;
    s->waiting = true;
}

bool maudStagedSwitching(const maudStaged* s)
{
    return s->waiting || s->fading || s->head.next != s->head.response;
}

// The tail's part of a response, or NULL; the head's starts it.
static const float* TailPart(const maudStaged* s, const float* response)
{
    return response != nullptr ? response + s->headFloats : nullptr;
}

// Channel c's block of the tail block in progress, into its region,
// crossfaded from the previous response when one is fading out.
static void RenderTail(maudStaged* s, uint32_t c)
{
    uint32_t t = s->plan.tailBlock;
    float* now = s->tail.samples;
    float* then = now + 2 * (size_t)t;
    float* region = s->tailOutput + ((size_t)((s->period - 1) % 2) * s->channels + c) * t;
    maudRenderPartitionedChannel(&s->tail, TailPart(s, s->response), c, now);
    if (!s->fading)
    {
        memcpy(region, now + t, (size_t)t * sizeof(float));
        return;
    }
    maudRenderPartitionedChannel(&s->tail, TailPart(s, s->previous), c, then);
    for (uint32_t i = 0; i < t; ++i)
    {
        float w = ((float)i + 0.5f) / (float)t;
        region[i] = then[t + i] + w * (now[t + i] - then[t + i]);
    }
}

// The work at a head block boundary: at the period's start the finished
// block goes into the tail's ring and a waiting response is taken; every
// step renders its share of the channels; the last hands the head the
// new response, to switch with the output at the next boundary.
static void Step(maudStaged* s)
{
    uint32_t b = s->plan.block;
    uint32_t t = s->plan.tailBlock;
    uint32_t steps = t / b;
    uint32_t k = s->position / b;
    if (s->position == t)
    {
        maudPushPartitionedBlock(&s->tail, s->tailInput);
        s->position = 0;
        s->period += 1;
        k = 0;
        if (s->waiting && s->head.next == s->head.response)
        {
            s->previous = s->response;
            s->response = s->pending;
            s->waiting = false;
            s->fading = true;
        }
    }
    if (s->period == 0)
    {
        return;
    }
    uint32_t first = k * s->channels / steps;
    uint32_t last = (k + 1) * s->channels / steps;
    for (uint32_t c = first; c < last; ++c)
    {
        RenderTail(s, c);
    }
    if (k == steps - 1 && s->fading)
    {
        maudSetPartitionedResponse(&s->head, s->response);
        s->fading = false;
        s->previous = nullptr;
    }
}

void maudRunStaged(maudStaged* s, const float* in, float* const* out, uint32_t frames)
{
    if (s->plan.tailBlock == 0)
    {
        if (s->waiting && s->head.next == s->head.response)
        {
            maudSetPartitionedResponse(&s->head, s->pending);
            s->response = s->pending;
            s->waiting = false;
        }
        maudRunPartitioned(&s->head, in, out, frames);
        return;
    }
    uint32_t b = s->plan.block;
    uint32_t t = s->plan.tailBlock;
    float* at[MAUD_STAGED_CHANNELS];
    for (uint32_t done = 0; done < frames;)
    {
        uint32_t count = b - s->position % b;
        count = count < frames - done ? count : frames - done;
        for (uint32_t c = 0; c < s->channels; ++c)
        {
            at[c] = out[c] + done;
        }
        maudRunPartitioned(&s->head, in + done, at, count);
        memcpy(s->tailInput + s->position, in + done, (size_t)count * sizeof(float));
        const float* region = s->tailOutput + (size_t)(s->period % 2) * s->channels * t;
        for (uint32_t c = 0; c < s->channels; ++c)
        {
            const float* from = region + (size_t)c * t + s->position;
            for (uint32_t i = 0; i < count; ++i)
            {
                at[c][i] += from[i];
            }
        }
        s->position += count;
        done += count;
        if (s->position % b == 0)
        {
            Step(s);
        }
    }
}
