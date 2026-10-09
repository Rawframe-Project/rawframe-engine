// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller (maul-audio/voice.h), as measured against Speex's
// on the AEC Challenge's synthetic clips. Frames are gathered into
// blocks; each block's
// capture goes through a DC notch (Speex's, two poles at radius 0.982
// at 16 kHz) and both signals through a pre-emphasis (0.6 at 16 kHz,
// which measured best between 0.4 and 0.9), the linear stage
// (echo_filter.h) takes the echo out, the output and the echo estimate
// are de-emphasized, and the suppressor (echo_suppressor.h) takes the
// noise and the echo left down. The notch's and the pre-emphasis'
// coefficients keep their responses in hertz at any rate. Blocks are
// the smallest power of two lasting 8 ms or more: 128 frames at 16 kHz,
// 512 at 48 kHz (measured, shorter blocks at 48 kHz cost the
// suppressor's frequency resolution more than the filter gained). The
// output lags the input by two blocks.

#include "allocator.h"
#include "echo_filter.h"
#include "echo_suppressor.h"
#include "real_fft.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <string.h>

#define ECHO_DEF_COOKIE 0x6D616563u
#define MIN_RATE        8000u
#define MAX_RATE        384000u
#define MIN_TAIL        0.05f
#define MAX_TAIL        1.0f
#define MIN_FLOOR_DB    (-40.0f)
#define MAX_FLOOR_DB    (-6.0f)
// The block's shortest length, in seconds.
#define MIN_BLOCK 0.008
// At 16 kHz: the notch's radius and the pre-emphasis.
#define NOTCH_16K   0.982
#define PREEMPH_16K 0.6
#define MEASURED_HZ 16000.0

struct maudEchoCanceller
{
    maudAllocator allocator;
    size_t bytes;
    uint32_t block;
    maudRealFft fft;
    maudEchoFilter filter;
    maudEchoSuppressor suppressor;
    // A block of the capture and of the render gathering, of the filter's
    // output and echo estimate, and of the output going out; where in the
    // block the stream is.
    float* capture;
    float* render;
    float* linear;
    float* echo;
    float* output;
    uint32_t at;
    // The notch's radius, its gain's denominator and its state; the
    // pre-emphasis and the last samples it saw and gave.
    double radius;
    double notchDen;
    double notch0;
    double notch1;
    float preemph;
    float lastCapture;
    float lastRender;
    float lastOutput;
    float lastEcho;
    uint64_t frames;
};

maudEchoCancellerDef maudDefaultEchoCancellerDef(void)
{
    return (maudEchoCancellerDef){
        .cookie = ECHO_DEF_COOKIE,
        .sampleRate = 48000,
        .tailSeconds = 0.2f,
        .floorDb = -15.0f,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static bool DefValid(const maudEchoCancellerDef* def)
{
    return def->cookie == ECHO_DEF_COOKIE && def->sampleRate >= MIN_RATE &&
           def->sampleRate <= MAX_RATE && def->tailSeconds >= MIN_TAIL &&
           def->tailSeconds <= MAX_TAIL && def->floorDb >= MIN_FLOOR_DB &&
           def->floorDb <= MAX_FLOOR_DB && maudIsAllocatorValid(&def->allocator);
}

// The smallest power of two of frames lasting MIN_BLOCK or more.
static uint32_t BlockOf(uint32_t rate)
{
    uint32_t block = 1;
    while ((double)block < MIN_BLOCK * rate)
    {
        block *= 2;
    }
    return block;
}

typedef struct Layout
{
    size_t fft;
    size_t filter;
    size_t suppressor;
    size_t floats;
    size_t total;
} Layout;

static Layout LayoutOf(uint32_t block, uint32_t partitions, double rate)
{
    maudLayout layout = {0};
    (void)maudLayoutAdd(&layout, 1, sizeof(struct maudEchoCanceller), alignof(double));
    Layout l;
    l.fft = maudLayoutAdd(&layout, maudRealFftBytes(2 * block), 1, alignof(double));
    l.filter =
        maudLayoutAdd(&layout, maudEchoFilterBytes(block, partitions, rate), 1, alignof(double));
    l.suppressor = maudLayoutAdd(&layout, maudEchoSuppressorBytes(block, rate), 1, alignof(double));
    l.floats = maudLayoutAdd(&layout, 5 * (size_t)block, sizeof(float), alignof(float));
    l.total = layout.overflow ? 0 : layout.size;
    return l;
}

maudResult maudCreateEchoCanceller(const maudEchoCancellerDef* def,
                                   maudEchoCanceller** cancellerOut)
{
    if (cancellerOut != nullptr)
    {
        *cancellerOut = nullptr;
    }
    if (def == nullptr || cancellerOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    double rate = (double)def->sampleRate;
    uint32_t block = BlockOf(def->sampleRate);
    // The partitions covering the path; a float's tail a hair past a whole
    // number of blocks (0.2 s at 16 kHz) does not take another.
    uint32_t partitions = (uint32_t)ceil((double)def->tailSeconds * rate / block - 1e-6);
    Layout l = LayoutOf(block, partitions, rate);
    unsigned char* memory =
        l.total > 0 ? maudAllocate(&def->allocator, l.total, alignof(double)) : nullptr;
    if (memory == nullptr)
    {
        return maud_errorCapacity;
    }
    maudEchoCanceller* c = (maudEchoCanceller*)memory;
    double radius = 1.0 - (1.0 - NOTCH_16K) * MEASURED_HZ / rate;
    *c = (maudEchoCanceller){
        .allocator = def->allocator,
        .bytes = l.total,
        .block = block,
        .radius = radius,
        .notchDen = radius * radius + 0.7 * (1.0 - radius) * (1.0 - radius),
        .preemph = (float)pow(PREEMPH_16K, MEASURED_HZ / rate),
    };
    maudInitRealFft(&c->fft, 2 * block, memory + l.fft);
    maudInitEchoFilter(&c->filter, block, partitions, rate, &c->fft, memory + l.filter);
    maudInitEchoSuppressor(&c->suppressor, block, rate, (double)def->floorDb, &c->fft,
                           memory + l.suppressor);
    float* f = (float*)(memory + l.floats);
    float** blocks[5] = {&c->capture, &c->render, &c->linear, &c->echo, &c->output};
    for (int i = 0; i < 5; ++i)
    {
        *blocks[i] = f + (size_t)i * block;
    }
    memset(f, 0, 5 * (size_t)block * sizeof(float));
    *cancellerOut = c;
    return maud_success;
}

void maudDestroyEchoCanceller(maudEchoCanceller* canceller)
{
    if (canceller != nullptr)
    {
        maudAllocator allocator = canceller->allocator;
        maudRelease(&allocator, canceller, canceller->bytes, alignof(double));
    }
}

// The front on a gathered block: the notch on the capture, then the
// pre-emphasis on both.
static void Front(maudEchoCanceller* c)
{
    double r = c->radius;
    for (uint32_t i = 0; i < c->block; ++i)
    {
        double in = (double)c->capture[i];
        double out = c->notch0 + in;
        c->notch0 = c->notch1 + 2.0 * (-in + r * out);
        c->notch1 = in - c->notchDen * out;
        float notched = (float)(r * out);
        float render = c->render[i];
        c->capture[i] = notched - c->preemph * c->lastCapture;
        c->render[i] = render - c->preemph * c->lastRender;
        c->lastCapture = notched;
        c->lastRender = render;
    }
}

// A gathered block through the front, the filter, the de-emphasis and
// the suppressor, into the output.
static void Block(maudEchoCanceller* c)
{
    Front(c);
    maudRunEchoFilter(&c->filter, c->render, c->capture, c->linear, c->echo);
    for (uint32_t i = 0; i < c->block; ++i)
    {
        c->linear[i] += c->preemph * c->lastOutput;
        c->lastOutput = c->linear[i];
        c->echo[i] += c->preemph * c->lastEcho;
        c->lastEcho = c->echo[i];
    }
    maudRunEchoSuppressor(&c->suppressor, c->linear, c->echo, c->filter.bandLeak,
                          c->filter.binsPerBand, c->output);
}

maudResult maudCancelEcho(maudEchoCanceller* canceller, float* capture, const float* render,
                          uint32_t frameCount, maudEchoState* stateOut)
{
    if (canceller == nullptr || (frameCount > 0 && (capture == nullptr || render == nullptr)))
    {
        return maud_errorInvalid;
    }
    maudEchoCanceller* c = canceller;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        c->capture[c->at] = capture[i];
        c->render[c->at] = render[i];
        capture[i] = c->output[c->at];
        if (++c->at == c->block)
        {
            Block(c);
            c->at = 0;
        }
    }
    c->frames += frameCount;
    if (stateOut != nullptr)
    {
        *stateOut = (maudEchoState){.leakage = (float)c->filter.leak,
                                    .adapted = c->filter.adapted,
                                    .speechProbability = (float)c->suppressor.frameProbability,
                                    .frames = c->frames};
    }
    return maud_success;
}
