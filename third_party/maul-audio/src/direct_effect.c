// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Direct effects. A call's band gains are air absorption times
// directivity times (1 - occlusion) + occlusion * transmission; the
// loudest band joins the host's gain as the overall gain and the bands
// become dB targets below it, floored at -24 dB, which the band
// equalizer meets. The overall gain ramps per sample across a call, the
// filters per 8 frames, and the filters are refitted only when a target
// moves by 0.05 dB or more; while every target is 0 dB the equalizer is
// skipped.

#include "air_absorption.h"
#include "allocator.h"
#include "band_eq.h"

#include "maul-audio/direct.h"

#include <math.h>
#include <string.h>

#define DIRECT_DEF_COOKIE 0x6D616465u
#define MIN_RATE          32000.0f
#define MAX_RATE          384000.0f
#define FLOOR_DB          (-24.0)
#define REFIT_DB          0.05
#define SILENT            1e-6f

struct maudDirectEffect
{
    maudAllocator allocator;
    float absorption[MAUD_DIRECT_BANDS];
    maudBandEqSetup setup;
    maudBandEqState state;
    maudBandEqFilters filters;
    // The targets the filters were fitted to, and whether all are 0 dB.
    double targets[MAUD_DIRECT_BANDS];
    bool flat;
    float overall;
    bool started;
};

maudDirectEffectDef maudDefaultDirectEffectDef(void)
{
    maudDirectEffectDef def = {
        .cookie = DIRECT_DEF_COOKIE,
        .sampleRate = 48000.0f,
        .allocator = {nullptr, nullptr, nullptr},
    };
    maudAirAbsorptionOf(20.0, 50.0, def.airAbsorption);
    return def;
}

maudDirectParams maudDefaultDirectParams(void)
{
    return (maudDirectParams){
        .gain = 1.0f,
        .distance = 0.0f,
        .occlusion = 0.0f,
        .transmission = {1.0f, 1.0f, 1.0f},
        .directivity = {1.0f, 1.0f, 1.0f},
    };
}

static bool Unit(float v)
{
    return v >= 0.0f && v <= 1.0f;
}

maudResult maudGetDirectivity(const maudDirectivityPattern* pattern, maudVector3 toListener,
                              float* directivityOut)
{
    if (pattern == nullptr || directivityOut == nullptr || !isfinite(toListener.x) ||
        !isfinite(toListener.y) || !isfinite(toListener.z))
    {
        return maud_errorInvalid;
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        if (!Unit(pattern->weight[b]) || !(pattern->power[b] >= 0.0f) ||
            !isfinite(pattern->power[b]))
        {
            return maud_errorInvalid;
        }
    }
    float length = sqrtf(toListener.x * toListener.x + toListener.y * toListener.y +
                         toListener.z * toListener.z);
    float cosine = length > 0.0f ? -toListener.z / length : 1.0f;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        float w = pattern->weight[b];
        directivityOut[b] = powf(fabsf((1.0f - w) + w * cosine), pattern->power[b]);
    }
    return maud_success;
}

static bool DefValid(const maudDirectEffectDef* def)
{
    if (def->cookie != DIRECT_DEF_COOKIE || !(def->sampleRate >= MIN_RATE) ||
        !(def->sampleRate <= MAX_RATE) || !maudIsAllocatorValid(&def->allocator))
    {
        return false;
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        if (!(def->airAbsorption[b] >= 0.0f) || !isfinite(def->airAbsorption[b]))
        {
            return false;
        }
    }
    return true;
}

// Flat filters, nothing heard.
static void Clear(maudDirectEffect* effect)
{
    double zero[MAUD_DIRECT_BANDS] = {0.0, 0.0, 0.0};
    maudDesignBandEq(&effect->setup, zero, &effect->filters);
    memset(&effect->state, 0, sizeof(effect->state));
    memcpy(effect->targets, zero, sizeof(zero));
    effect->flat = true;
    effect->overall = 0.0f;
    effect->started = false;
}

maudResult maudCreateDirectEffect(const maudDirectEffectDef* def, maudDirectEffect** effectOut)
{
    if (effectOut != nullptr)
    {
        *effectOut = nullptr;
    }
    if (def == nullptr || effectOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudDirectEffect* effect =
        maudAllocate(&def->allocator, sizeof(maudDirectEffect), alignof(maudDirectEffect));
    if (effect == nullptr)
    {
        return maud_errorCapacity;
    }
    *effect = (maudDirectEffect){.allocator = def->allocator};
    memcpy(effect->absorption, def->airAbsorption, sizeof(effect->absorption));
    maudSetupBandEq(&effect->setup, def->sampleRate);
    Clear(effect);
    *effectOut = effect;
    return maud_success;
}

void maudDestroyDirectEffect(maudDirectEffect* effect)
{
    if (effect == nullptr)
    {
        return;
    }
    maudAllocator allocator = effect->allocator;
    maudRelease(&allocator, effect, sizeof(maudDirectEffect), alignof(maudDirectEffect));
}

maudResult maudResetDirectEffect(maudDirectEffect* effect)
{
    if (effect == nullptr)
    {
        return maud_errorInvalid;
    }
    Clear(effect);
    return maud_success;
}

static bool ParamsValid(const maudDirectParams* p)
{
    if (!(p->gain >= 0.0f) || !isfinite(p->gain) || !(p->distance >= 0.0f) ||
        !isfinite(p->distance) || !Unit(p->occlusion))
    {
        return false;
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        if (!Unit(p->transmission[b]) || !Unit(p->directivity[b]))
        {
            return false;
        }
    }
    return true;
}

// The call's overall gain and band targets in dB (0 at the loudest band,
// floored below it).
static float Targets(const maudDirectEffect* effect, const maudDirectParams* p, double* targets)
{
    float bands[MAUD_DIRECT_BANDS];
    float loudest = 0.0f;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        float air = expf(-effect->absorption[b] * p->distance);
        float through = (1.0f - p->occlusion) + p->occlusion * p->transmission[b];
        bands[b] = air * p->directivity[b] * through;
        loudest = fmaxf(loudest, bands[b]);
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        targets[b] = loudest > SILENT
                         ? fmax(FLOOR_DB, 20.0 * log10((double)bands[b] / (double)loudest))
                         : 0.0;
    }
    return loudest > SILENT ? p->gain * loudest : 0.0f;
}

static bool Moved(const double* from, const double* to)
{
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        if (fabs(to[b] - from[b]) >= REFIT_DB)
        {
            return true;
        }
    }
    return false;
}

// Brings the filters to the targets if they moved, previous receiving
// the filters the call starts from; true if they changed.
static bool Refit(maudDirectEffect* effect, const double* targets, maudBandEqFilters* previous)
{
    *previous = effect->filters;
    if (!Moved(effect->targets, targets))
    {
        return false;
    }
    double gains[MAUD_DIRECT_BANDS];
    maudSolveBandEq(&effect->setup, targets, gains);
    maudDesignBandEq(&effect->setup, gains, &effect->filters);
    memcpy(effect->targets, targets, sizeof(effect->targets));
    effect->flat = targets[0] == 0.0 && targets[1] == 0.0 && targets[2] == 0.0;
    return true;
}

maudResult maudProcessDirect(maudDirectEffect* effect, const maudDirectParams* params,
                             const float* in, float* out, uint32_t frames)
{
    if (effect == nullptr || params == nullptr || !ParamsValid(params) ||
        (frames > 0 && (in == nullptr || out == nullptr)))
    {
        return maud_errorInvalid;
    }
    if (frames == 0)
    {
        return maud_success;
    }
    double targets[MAUD_DIRECT_BANDS];
    float overall = Targets(effect, params, targets);
    bool wasFlat = effect->flat;
    maudBandEqFilters previous;
    bool steady = !Refit(effect, targets, &previous);
    if (!effect->started)
    {
        steady = true;
        previous = effect->filters;
        wasFlat = effect->flat;
        effect->overall = overall;
        effect->started = true;
    }
    if (wasFlat && effect->flat)
    {
        // Flat filters pass their input whatever their state; the state
        // starts from zero when they next change, which a ramp's first
        // small steps leave time to settle.
        memset(&effect->state, 0, sizeof(effect->state));
        if (out != in)
        {
            memmove(out, in, (size_t)frames * sizeof(float));
        }
    }
    else
    {
        maudRunBandEq(&effect->state, steady ? &effect->filters : &previous, &effect->filters, in,
                      out, frames);
    }
    float start = effect->overall;
    float step = (overall - start) / (float)frames;
    for (uint32_t n = 0; n < frames; ++n)
    {
        out[n] *= start + step * (float)(n + 1);
    }
    effect->overall = overall;
    return maud_success;
}
