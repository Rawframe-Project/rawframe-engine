// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Partitioned convolution (partitioned.h). Partition p of a response is
// its samples [p b, (p + 1) b) followed by b zeros, transformed; a
// block's output is the last b samples of the inverse of the sum over p
// of the input spectrum p blocks back times partition p: overlap-save.

#include "partitioned.h"

#include <string.h>

static size_t Bins(uint32_t block)
{
    return (size_t)(block + 1) * 2;
}

size_t maudResponseFloats(uint32_t block, uint32_t partitions, uint32_t channels)
{
    return (size_t)channels * partitions * Bins(block);
}

size_t maudPartitionedBytes(uint32_t block, uint32_t partitions, uint32_t channels)
{
    size_t floats = 2 * (size_t)block + (size_t)partitions * Bins(block) +
                    (size_t)channels * block + Bins(block) + 4 * (size_t)block;
    return maudRealFftBytes(2 * block) + floats * sizeof(float);
}

void maudInitPartitioned(maudPartitioned* p, uint32_t block, uint32_t partitions, uint32_t channels,
                         void* memory)
{
    *p = (maudPartitioned){.block = block, .partitions = partitions, .channels = channels};
    maudInitRealFft(&p->fft, 2 * block, memory);
    float* f = (float*)((unsigned char*)memory + maudRealFftBytes(2 * block));
    p->input = f;
    f += 2 * (size_t)block;
    p->ring = f;
    f += (size_t)partitions * Bins(block);
    p->output = f;
    f += (size_t)channels * block;
    p->spectrum = f;
    f += Bins(block);
    p->samples = f;
    maudResetPartitioned(p);
}

void maudResetPartitioned(maudPartitioned* p)
{
    memset(p->input, 0, 2 * (size_t)p->block * sizeof(float));
    memset(p->ring, 0, (size_t)p->partitions * Bins(p->block) * sizeof(float));
    memset(p->output, 0, (size_t)p->channels * p->block * sizeof(float));
    p->filled = 0;
    p->newest = 0;
}

void maudPartitionResponse(const maudPartitioned* p, const float* const* samples, uint32_t frames,
                           float* response, float* scratch)
{
    uint32_t b = p->block;
    float* padded = scratch;
    float* bins = scratch + 2 * (size_t)b;
    for (uint32_t c = 0; c < p->channels; ++c)
    {
        for (uint32_t k = 0; k < p->partitions; ++k)
        {
            memset(padded, 0, 2 * (size_t)b * sizeof(float));
            uint64_t first = (uint64_t)k * b;
            for (uint32_t i = 0; i < b && first + i < frames; ++i)
            {
                padded[i] = samples[c][first + i];
            }
            maudForwardRealFft(&p->fft, padded, bins);
            memcpy(response + ((size_t)c * p->partitions + k) * Bins(b), bins,
                   Bins(b) * sizeof(float));
        }
    }
}

void maudSetPartitionedResponse(maudPartitioned* p, const float* response)
{
    p->next = response;
}

// A channel's spectrum: the ring times the response's partitions.
static void Accumulate(const maudPartitioned* p, const float* response, uint32_t channel,
                       float* spectrum)
{
    size_t bins = Bins(p->block);
    memset(spectrum, 0, bins * sizeof(float));
    const float* h = response + (size_t)channel * p->partitions * bins;
    for (uint32_t k = 0; k < p->partitions; ++k)
    {
        uint32_t slot = (p->newest + p->partitions - k) % p->partitions;
        const float* x = p->ring + (size_t)slot * bins;
        const float* g = h + (size_t)k * bins;
        for (size_t i = 0; i < bins; i += 2)
        {
            spectrum[i] += x[i] * g[i] - x[i + 1] * g[i + 1];
            spectrum[i + 1] += x[i] * g[i + 1] + x[i + 1] * g[i];
        }
    }
}

// A channel's next block under a response (zeros for none) into
// samples' second half.
static void Render(maudPartitioned* p, const float* response, uint32_t channel, float* samples)
{
    if (response == nullptr)
    {
        memset(samples + p->block, 0, (size_t)p->block * sizeof(float));
        return;
    }
    Accumulate(p, response, channel, p->spectrum);
    maudInverseRealFft(&p->fft, p->spectrum, samples);
}

// A full input block: its spectrum into the ring, every channel's next
// output block, a new response faded in.
static void Block(maudPartitioned* p)
{
    uint32_t b = p->block;
    p->newest = (p->newest + 1) % p->partitions;
    maudForwardRealFft(&p->fft, p->input, p->ring + (size_t)p->newest * Bins(b));
    bool fade = p->next != p->response;
    float* now = p->samples;
    float* then = p->samples + 2 * (size_t)b;
    for (uint32_t c = 0; c < p->channels; ++c)
    {
        float* out = p->output + (size_t)c * b;
        Render(p, p->next, c, now);
        if (!fade)
        {
            memcpy(out, now + b, (size_t)b * sizeof(float));
            continue;
        }
        Render(p, p->response, c, then);
        for (uint32_t i = 0; i < b; ++i)
        {
            float w = ((float)i + 0.5f) / (float)b;
            out[i] = then[b + i] + w * (now[b + i] - then[b + i]);
        }
    }
    p->response = p->next;
    memmove(p->input, p->input + b, (size_t)b * sizeof(float));
}

void maudRunPartitioned(maudPartitioned* p, const float* in, float* const* out, uint32_t frames)
{
    uint32_t b = p->block;
    for (uint32_t done = 0; done < frames;)
    {
        uint32_t count = b - p->filled < frames - done ? b - p->filled : frames - done;
        memcpy(p->input + b + p->filled, in + done, (size_t)count * sizeof(float));
        for (uint32_t c = 0; c < p->channels; ++c)
        {
            const float* from = p->output + (size_t)c * b + p->filled;
            float* to = out[c] + done;
            for (uint32_t i = 0; i < count; ++i)
            {
                to[i] += from[i];
            }
        }
        p->filled += count;
        done += count;
        if (p->filled == b)
        {
            Block(p);
            p->filled = 0;
        }
    }
}
