// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverbs. Sixteen delay lines of 23 to 69 ms (prime lengths at the
// rate), each read through its octave attenuation filter, mixed by a
// 16-point Hadamard matrix (scaled to be orthogonal) and written back
// with the send. The filters' targets are each line's loss per pass,
// -60 dB times its delay over the reverberation time, octave by octave,
// with the band times placed at the bands' geometric centres and
// interpolated in log time over log frequency. Each line leaves into the
// bed from one of 16 directions spread over the sphere. A change of times
// ramps the filters in steps of 8 frames: a biquad is stable for any
// denominator inside a triangle, which holds every blend of two stable
// ones. A reverb with a tail has a second such network for the slower
// slope, its lines between the first's (24 to 72 ms) so that the two do
// not ring together, fed the same delayed send at its own levels.

#include "maul-audio/reverb.h"

#include "allocator.h"
#include "band_eq.h"
#include "octave_eq.h"

#include <math.h>
#include <string.h>

#define REVERB_DEF_COOKIE 0x6D617276u
#define LINES             16
#define FILTERS           MAUD_OCTAVE_FILTERS
#define RAMP_FRAMES       8u
// The send enters line i negated where bit i is set: with one sign on
// every line the lines stay correlated, W sums them coherently and the
// directional channels carry a sixth of its energy rather than the
// diffuse third.
#define SEND_SIGNS 0x5A3Cu
#define MIN_RATE   44100.0f
#define MAX_RATE   384000.0f
#define MIN_TIME   0.1f
#define MAX_TIME   20.0f
#define MAX_DELAY  4.0f
#define MIN_LEVEL  (-96.0f)
#define MAX_LEVEL  24.0f
// Levels are redesigned past this change, in dB.
#define RELEVEL 0.01f
// The send is delayed and leveled this many frames at a time.
#define CHUNK 256u
// Times are refitted when one moves by more than this share, a tenth of
// the 5 % that is just noticeable.
#define REFIT 0.005f
#define PI_D  3.14159265358979323846
// A tail whose send has stopped runs on until it has fallen this far.
#define TAIL_RING_DB 80.0

// Every line's filters, structure of arrays so that lines vectorize.
typedef struct Bank
{
    float b0[FILTERS][LINES];
    float b1[FILTERS][LINES];
    float b2[FILTERS][LINES];
    float a1[FILTERS][LINES];
    float a2[FILTERS][LINES];
    float gain[LINES];
} Bank;

// One network: its lines, their filters, and the leveling of its send.
typedef struct Network
{
    uint32_t lengths[LINES];
    uint32_t positions[LINES];
    float* lines[LINES];
    // Its lines' samples, one block.
    size_t floats;
    float s1[FILTERS][LINES];
    float s2[FILTERS][LINES];
    // The bed's gains per line: W, Y, Z, X.
    float encode[4][LINES];
    Bank current;
    Bank target;
    float times[MAUD_DIRECT_BANDS];
    // Whether it has run since made, reset or cleared, and whether its
    // filters ramp in this call.
    bool started;
    bool ramp;
    // The send's levels: the equalizer's filters now and to come, its
    // state, the levels they meet, whether all are 0 dB.
    maudBandEqFilters eqCurrent;
    maudBandEqFilters eqTarget;
    maudBandEqState eqState;
    float levels[MAUD_DIRECT_BANDS];
    bool flat;
    bool leveling;
} Network;

struct maudReverb
{
    maudAllocator allocator;
    double rate;
    maudOctaveEqSetup setup;
    maudBandEqSetup eqSetup;
    float* memory;
    size_t memoryFloats;
    // The send's delay line (the longest delay and one sample), where
    // the next sample goes, and the delay now.
    float* delayLine;
    uint32_t delayLength;
    uint32_t delayAt;
    // The first slope's network, and the tail's for a reverb made with
    // one.
    Network networks[2];
    uint32_t networkCount;
    // Whether the tail's network runs, and the frames it runs on once its
    // send has stopped.
    bool tailRunning;
    uint64_t tailLeft;
};

maudReverbDef maudDefaultReverbDef(void)
{
    return (maudReverbDef){
        .cookie = REVERB_DEF_COOKIE,
        .sampleRate = 48000.0f,
        .maxDelay = 0.0f,
        .tail = false,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static bool Prime(uint32_t n)
{
    if (n < 2)
    {
        return false;
    }
    for (uint32_t d = 2; d * d <= n; ++d)
    {
        if (n % d == 0)
        {
            return false;
        }
    }
    return true;
}

// Lengths from 23 ms up by thirds of an octave of time, offset by a
// share of a step.
static void Lengths(double rate, double offset, uint32_t* lengths)
{
    for (int i = 0; i < LINES; ++i)
    {
        double ms = 23.0 * pow(3.0, ((double)i + offset) / (LINES - 1));
        uint32_t n = (uint32_t)(ms * rate / 1000.0);
        while (!Prime(n) || (i > 0 && n <= lengths[i - 1]))
        {
            ++n;
        }
        lengths[i] = n;
    }
}

// Sixteen directions on the sphere (a Fibonacci lattice), first-order
// SN3D gains in the field's axes (x ahead, y left, z up), a quarter
// each.
static void Directions(float encode[4][LINES])
{
    for (int i = 0; i < LINES; ++i)
    {
        double z = 1.0 - 2.0 * ((double)i + 0.5) / LINES;
        double a = PI_D * (1.0 + sqrt(5.0)) * ((double)i + 0.5);
        double r = sqrt(1.0 - z * z);
        encode[0][i] = 0.25f;
        encode[1][i] = (float)(0.25 * r * sin(a));
        encode[2][i] = (float)(0.25 * z);
        encode[3][i] = (float)(0.25 * r * cos(a));
    }
}

maudResult maudCreateReverb(const maudReverbDef* def, maudReverb** reverbOut)
{
    if (reverbOut != nullptr)
    {
        *reverbOut = nullptr;
    }
    if (def == nullptr || reverbOut == nullptr || def->cookie != REVERB_DEF_COOKIE ||
        !(def->sampleRate >= MIN_RATE) || !(def->sampleRate <= MAX_RATE) ||
        !(def->maxDelay >= 0.0f) || !(def->maxDelay <= MAX_DELAY) ||
        !maudIsAllocatorValid(&def->allocator))
    {
        return maud_errorInvalid;
    }
    maudReverb* r = maudAllocate(&def->allocator, sizeof(maudReverb), alignof(maudReverb));
    if (r == nullptr)
    {
        return maud_errorCapacity;
    }
    *r = (maudReverb){.allocator = def->allocator,
                      .rate = (double)def->sampleRate,
                      .networkCount = def->tail ? 2u : 1u};
    for (uint32_t k = 0; k < r->networkCount; ++k)
    {
        Network* n = &r->networks[k];
        Lengths(r->rate, k == 0 ? 0.0 : 0.5, n->lengths);
        for (int i = 0; i < LINES; ++i)
        {
            n->floats += n->lengths[i];
        }
        Directions(n->encode);
        r->memoryFloats += n->floats;
    }
    r->delayLength = (uint32_t)ceil((double)def->maxDelay * r->rate) + 1;
    r->memoryFloats += r->delayLength;
    r->memory = maudAllocate(&def->allocator, r->memoryFloats * sizeof(float), alignof(float));
    if (r->memory == nullptr)
    {
        maudRelease(&def->allocator, r, sizeof(maudReverb), alignof(maudReverb));
        return maud_errorCapacity;
    }
    size_t offset = 0;
    for (uint32_t k = 0; k < r->networkCount; ++k)
    {
        for (int i = 0; i < LINES; ++i)
        {
            r->networks[k].lines[i] = r->memory + offset;
            offset += r->networks[k].lengths[i];
        }
    }
    r->delayLine = r->memory + offset;
    maudSetupOctaveEq(&r->setup, r->rate);
    maudSetupBandEq(&r->eqSetup, def->sampleRate);
    if (maudResetReverb(r) != maud_success)
    {
        maudDestroyReverb(r);
        return maud_errorInvalid;
    }
    *reverbOut = r;
    return maud_success;
}

void maudDestroyReverb(maudReverb* reverb)
{
    if (reverb == nullptr)
    {
        return;
    }
    maudAllocator allocator = reverb->allocator;
    maudRelease(&allocator, reverb->memory, reverb->memoryFloats * sizeof(float), alignof(float));
    maudRelease(&allocator, reverb, sizeof(maudReverb), alignof(maudReverb));
}

// Silences a network: its lines, filters and leveling.
static void Clear(Network* n)
{
    memset(n->lines[0], 0, n->floats * sizeof(float));
    memset(n->positions, 0, sizeof(n->positions));
    memset(n->s1, 0, sizeof(n->s1));
    memset(n->s2, 0, sizeof(n->s2));
    memset(&n->eqState, 0, sizeof(n->eqState));
    n->started = false;
}

maudResult maudResetReverb(maudReverb* reverb)
{
    if (reverb == nullptr)
    {
        return maud_errorInvalid;
    }
    for (uint32_t k = 0; k < reverb->networkCount; ++k)
    {
        Clear(&reverb->networks[k]);
    }
    memset(reverb->delayLine, 0, reverb->delayLength * sizeof(float));
    reverb->delayAt = 0;
    reverb->tailRunning = false;
    return maud_success;
}

// The time at an octave: the band times at the bands' geometric centres,
// interpolated in log time over log frequency, held beyond the ends.
static double OctaveTime(const float* times, double hz)
{
    const double centres[MAUD_DIRECT_BANDS] = {sqrt(20.0 * 800.0), sqrt(800.0 * 8000.0),
                                               sqrt(8000.0 * 20000.0)};
    if (hz <= centres[0])
    {
        return (double)times[0];
    }
    for (int b = 0; b + 1 < MAUD_DIRECT_BANDS; ++b)
    {
        if (hz <= centres[b + 1])
        {
            double u = log(hz / centres[b]) / log(centres[b + 1] / centres[b]);
            return exp((1.0 - u) * log((double)times[b]) + u * log((double)times[b + 1]));
        }
    }
    return (double)times[MAUD_DIRECT_BANDS - 1];
}

// Designs every line's filters for the times into bank.
static void Design(const maudReverb* r, const Network* n, const float* times, Bank* bank)
{
    // Every line's targets have one shape, scaled by its length: one fit
    // serves them all.
    double shape[MAUD_OCTAVES];
    for (int m = 0; m < MAUD_OCTAVES; ++m)
    {
        shape[m] = -60.0 / OctaveTime(times, maudOctaveCentre(m));
    }
    double solve[MAUD_OCTAVES * MAUD_OCTAVE_POINTS];
    bool fitted = maudFitOctaveEq(&r->setup, shape, solve);
    for (int i = 0; i < LINES; ++i)
    {
        double targets[MAUD_OCTAVES];
        for (int m = 0; m < MAUD_OCTAVES; ++m)
        {
            targets[m] = shape[m] * (double)n->lengths[i] / r->rate;
        }
        maudBiquad filters[FILTERS];
        float gain = 0.0f;
        if (fitted)
        {
            maudDesignOctaveEq(&r->setup, solve, targets, filters, &gain);
        }
        else
        {
            // The fit's system is fixed and well posed; should it ever
            // fail, the lines fall silent rather than ring.
            for (int f = 0; f < FILTERS; ++f)
            {
                filters[f] = (maudBiquad){1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
        for (int f = 0; f < FILTERS; ++f)
        {
            bank->b0[f][i] = filters[f].b0;
            bank->b1[f][i] = filters[f].b1;
            bank->b2[f][i] = filters[f].b2;
            bank->a1[f][i] = filters[f].a1;
            bank->a2[f][i] = filters[f].a2;
        }
        bank->gain[i] = gain;
    }
}

// The 16-point Hadamard transform in place, scaled by 1/4.
static void Hadamard(float* v)
{
    for (int span = 1; span < LINES; span *= 2)
    {
        for (int i = 0; i < LINES; i += 2 * span)
        {
            for (int j = i; j < i + span; ++j)
            {
                float a = v[j];
                float b = v[j + span];
                v[j] = a + b;
                v[j + span] = a - b;
            }
        }
    }
    for (int i = 0; i < LINES; ++i)
    {
        v[i] *= 0.25f;
    }
}

// Lines' outputs through their filters.
static void Filter(Network* r, const Bank* k, float* v)
{
    for (int f = 0; f < FILTERS; ++f)
    {
        for (int i = 0; i < LINES; ++i)
        {
            float x = v[i];
            float y = k->b0[f][i] * x + r->s1[f][i];
            r->s1[f][i] = k->b1[f][i] * x - k->a1[f][i] * y + r->s2[f][i];
            r->s2[f][i] = k->b2[f][i] * x - k->a2[f][i] * y;
            v[i] = y;
        }
    }
    for (int i = 0; i < LINES; ++i)
    {
        v[i] *= k->gain[i];
    }
}

static void Run(Network* r, const Bank* k, const float* in, float* const* bed, uint32_t count)
{
    for (uint32_t n = 0; n < count; ++n)
    {
        float v[LINES];
        for (int i = 0; i < LINES; ++i)
        {
            v[i] = r->lines[i][r->positions[i]];
        }
        Filter(r, k, v);
        for (int c = 0; c < 4; ++c)
        {
            float sum = 0.0f;
            for (int i = 0; i < LINES; ++i)
            {
                sum += r->encode[c][i] * v[i];
            }
            bed[c][n] += sum;
        }
        Hadamard(v);
        float send = 0.25f * in[n];
        for (int i = 0; i < LINES; ++i)
        {
            r->lines[i][r->positions[i]] = v[i] + ((SEND_SIGNS >> i) & 1u ? -send : send);
            r->positions[i] = r->positions[i] + 1 == r->lengths[i] ? 0 : r->positions[i] + 1;
        }
    }
}

static void Blend(const Bank* from, const Bank* to, float t, Bank* out)
{
    const float* a = &from->b0[0][0];
    const float* b = &to->b0[0][0];
    float* o = &out->b0[0][0];
    // The bank is floats throughout: blend it as one array.
    size_t count = sizeof(Bank) / sizeof(float);
    for (size_t i = 0; i < count; ++i)
    {
        o[i] = a[i] + t * (b[i] - a[i]);
    }
}

static bool ParamsValid(const maudReverb* r, const maudReverbParams* p)
{
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        if (!(p->reverbTime[b] >= MIN_TIME) || !(p->reverbTime[b] <= MAX_TIME) ||
            !(p->level[b] >= MIN_LEVEL) || !(p->level[b] <= MAX_LEVEL) ||
            !(p->tailTime[b] == 0.0f ||
              (p->tailTime[b] >= MIN_TIME && p->tailTime[b] <= MAX_TIME)) ||
            !(p->tailLevel[b] >= MIN_LEVEL) || !(p->tailLevel[b] <= MAX_LEVEL))
        {
            return false;
        }
    }
    return p->delay >= 0.0f && (double)p->delay * r->rate < (double)r->delayLength;
}

// Takes a network's new times and levels: designs what changed, at once
// if it has not run since made, reset or cleared; its filters ramp
// otherwise when its times change.
static void Retarget(const maudReverb* r, Network* n, const float* times, const float* levels)
{
    bool changed = !n->started;
    bool leveled = !n->started;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        changed = changed || fabsf(times[b] - n->times[b]) > REFIT * n->times[b];
        leveled = leveled || fabsf(levels[b] - n->levels[b]) > RELEVEL;
    }
    if (changed)
    {
        Design(r, n, times, &n->target);
        memcpy(n->times, times, sizeof(n->times));
    }
    if (leveled)
    {
        double targets[MAUD_DIRECT_BANDS];
        double gains[MAUD_DIRECT_BANDS];
        n->flat = true;
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            targets[b] = (double)levels[b];
            n->flat = n->flat && levels[b] == 0.0f;
        }
        maudSolveBandEq(&r->eqSetup, targets, gains);
        maudDesignBandEq(&r->eqSetup, gains, &n->eqTarget);
        memcpy(n->levels, levels, sizeof(n->levels));
        n->leveling = true;
    }
    n->ramp = changed && n->started;
    if (!n->started)
    {
        n->current = n->target;
        n->eqCurrent = n->eqTarget;
        n->leveling = false;
        n->started = true;
    }
}

// Takes the tail's times and levels; returns whether its network runs in
// this call. A band without a tail is silent in it, at the first slope's
// time; with a tail in no band the send stops, and the network runs on
// until its sound has fallen TAIL_RING_DB, then is cleared.
static bool RetargetTail(maudReverb* r, const maudReverbParams* p, uint32_t frames)
{
    if (r->networkCount < 2)
    {
        return false;
    }
    Network* n = &r->networks[1];
    float times[MAUD_DIRECT_BANDS];
    float levels[MAUD_DIRECT_BANDS];
    bool any = false;
    double longest = 0.0;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        bool has = p->tailTime[b] > 0.0f;
        any = any || has;
        times[b] = has ? p->tailTime[b] : p->reverbTime[b];
        levels[b] = has ? p->tailLevel[b] : MIN_LEVEL;
        longest = fmax(longest, (double)times[b]);
    }
    if (any)
    {
        r->tailRunning = true;
        r->tailLeft = (uint64_t)ceil(TAIL_RING_DB / 60.0 * longest * r->rate);
    }
    else
    {
        if (!r->tailRunning)
        {
            return false;
        }
        if (r->tailLeft <= frames)
        {
            Clear(n);
            r->tailRunning = false;
            return false;
        }
        r->tailLeft -= frames;
        memcpy(times, n->times, sizeof(times));
    }
    Retarget(r, n, times, levels);
    return true;
}

static void BlendEq(const maudBandEqFilters* from, const maudBandEqFilters* to, float t,
                    maudBandEqFilters* out)
{
    const float* a = &from->g[0];
    const float* b = &to->g[0];
    float* o = &out->g[0];
    size_t count = sizeof(maudBandEqFilters) / sizeof(float);
    for (size_t i = 0; i < count; ++i)
    {
        o[i] = a[i] + t * (b[i] - a[i]);
    }
}

// count frames of the send, delayed by delay samples, into x.
static void Delay(maudReverb* r, const float* in, uint32_t delay, uint32_t count, float* x)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        r->delayLine[r->delayAt] = in[i];
        uint32_t from = r->delayAt + r->delayLength - delay;
        x[i] = r->delayLine[from >= r->delayLength ? from - r->delayLength : from];
        r->delayAt = r->delayAt + 1 == r->delayLength ? 0 : r->delayAt + 1;
    }
}

// Frames [done, done + count) of frames of the delayed send in x, leveled
// for a network (the levels moving across the call).
static void Level(Network* r, uint32_t done, uint32_t count, uint32_t frames, float* x)
{
    if (r->flat && !r->leveling)
    {
        return;
    }
    maudBandEqFilters from;
    maudBandEqFilters to;
    BlendEq(&r->eqCurrent, &r->eqTarget, (float)done / (float)frames, &from);
    BlendEq(&r->eqCurrent, &r->eqTarget, (float)(done + count) / (float)frames, &to);
    maudRunBandEq(&r->eqState, &from, &to, x, x, count);
}

// A network's frames [done, done + count) of frames, from its leveled
// send in x, added into the bed; its filters ramp across the call in
// steps of RAMP_FRAMES when its times change.
static void Render(Network* n, const float* x, float* const* bed, uint32_t done, uint32_t count,
                   uint32_t frames)
{
    for (uint32_t start = 0; start < count; start += RAMP_FRAMES)
    {
        uint32_t m = count - start < RAMP_FRAMES ? count - start : RAMP_FRAMES;
        float* const segment[4] = {bed[0] + done + start, bed[1] + done + start,
                                   bed[2] + done + start, bed[3] + done + start};
        if (!n->ramp)
        {
            Run(n, &n->current, x + start, segment, m);
            continue;
        }
        Bank at;
        Blend(&n->current, &n->target, (float)(done + start + m) / (float)frames, &at);
        Run(n, &at, x + start, segment, m);
    }
}

// A network's filters and levels after a call: those it moved to.
static void Settle(Network* n)
{
    n->current = n->target;
    n->eqCurrent = n->eqTarget;
    n->leveling = false;
    n->ramp = false;
}

maudResult maudProcessReverb(maudReverb* reverb, const maudReverbParams* params, const float* in,
                             float* const* bed, uint32_t frames)
{
    if (reverb == nullptr || params == nullptr || !ParamsValid(reverb, params) ||
        (frames > 0 && (in == nullptr || bed == nullptr || bed[0] == nullptr || bed[1] == nullptr ||
                        bed[2] == nullptr || bed[3] == nullptr)))
    {
        return maud_errorInvalid;
    }
    if (frames == 0)
    {
        return maud_success;
    }
    Network* first = &reverb->networks[0];
    Network* tail = &reverb->networks[1];
    Retarget(reverb, first, params->reverbTime, params->level);
    bool tailed = RetargetTail(reverb, params, frames);
    uint32_t delay = (uint32_t)lround((double)params->delay * reverb->rate);
    float x[CHUNK];
    float y[CHUNK];
    for (uint32_t done = 0; done < frames; done += CHUNK)
    {
        uint32_t count = frames - done < CHUNK ? frames - done : CHUNK;
        Delay(reverb, in + done, delay, count, x);
        if (tailed)
        {
            memcpy(y, x, count * sizeof(float));
        }
        Level(first, done, count, frames, x);
        Render(first, x, bed, done, count, frames);
        if (tailed)
        {
            Level(tail, done, count, frames, y);
            Render(tail, y, bed, done, count, frames);
        }
    }
    Settle(first);
    if (tailed)
    {
        Settle(tail);
    }
    return maud_success;
}
