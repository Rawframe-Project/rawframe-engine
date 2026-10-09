// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural effects: each ear blends the four measured responses and
// delays around the source's direction (as that ear sees it, near the
// head), reads its delayed signal from the input's history by cubic
// interpolation, filters it for the head's near field and convolves it
// with a direct-form FIR. A change runs the old and new responses side
// by side for a fixed fade, crossfading them while the delays and the
// near-field filters move.
// One block from the def's allocator holds everything; processing only
// reads and writes it.

#include "maul-audio/binaural.h"

#include "allocator.h"
#include "binaural_dsp.h"
#include "hrtf_core.h"
#include "hrtf_lookup.h"
#include "near_field.h"

#include <math.h>
#include <string.h>

#define BINAURAL_DEF_COOKIE 0x6D616262u
#define MAX_FRAMES          16384u
#define DEFAULT_MAX_FRAMES  1024u
// The fade: 128 frames at 48 kHz, in proportion at other rates.
#define FADE_SECONDS (128.0 / 48000.0)
// Every delay is lengthened by one sample, so the cubic read never needs
// a sample later than the one it writes.
#define BASE_DELAY 1.0f
#define DEGREES    57.29577951308232f
#define MIN_RADIUS 0.05f
#define MAX_RADIUS 0.15f
// The near field's nearest distance: closer sources count as this close.
#define MIN_DISTANCE 0.1f

// An ear pair's responses, delays and near-field filters ({b0, b1, a1}).
typedef struct Filter
{
    float* response[2];
    float delay[2];
    float shelf[2][3];
} Filter;

struct maudBinaural
{
    maudAllocator allocator;
    size_t bytes;
    const maudHrtf* hrtf;
    uint32_t maxFrames;
    uint32_t taps;
    uint32_t fadeFrames;
    // The input's history kept before each call's frames: the largest
    // delay and the cubic read's reach.
    uint32_t reach;
    float* history;
    // Each ear's delayed signal, taps - 1 samples of history first.
    float* ears[2];
    // The new filter's output during a fade.
    float* scratch;
    bool nearField;
    float headRadius;
    // Each ear's near-field filter memory: x[n - 1], y[n - 1].
    float shelfState[2][2];
    Filter current;
    Filter next;
    // Where the filters are for, as Key gives it.
    maudVector3 currentKey;
    maudVector3 nextKey;
    maudVector3 wantedKey;
    // Frames left in the fade under way; 0 when none is.
    uint32_t fadeLeft;
    float gain;
    bool primed;
};

maudBinauralDef maudDefaultBinauralDef(void)
{
    return (maudBinauralDef){
        .cookie = BINAURAL_DEF_COOKIE,
        .hrtf = nullptr,
        .maxFrames = DEFAULT_MAX_FRAMES,
        .nearField = true,
        .headRadius = 0.0875f,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static bool DefValid(const maudBinauralDef* def)
{
    return def->cookie == BINAURAL_DEF_COOKIE && def->hrtf != nullptr && def->maxFrames >= 1 &&
           def->maxFrames <= MAX_FRAMES && def->headRadius >= MIN_RADIUS &&
           def->headRadius <= MAX_RADIUS && maudIsAllocatorValid(&def->allocator);
}

// The history the delays need: the set's largest delay, the base delay,
// and the cubic read's two samples further back.
static uint32_t Reach(const maudHrtf* hrtf)
{
    float largest = 0.0f;
    for (size_t i = 0; i < 2u * (size_t)hrtf->directionCount; ++i)
    {
        largest = hrtf->delays[i] > largest ? hrtf->delays[i] : largest;
    }
    return (uint32_t)ceilf(largest + BASE_DELAY) + 3u;
}

typedef struct Parts
{
    size_t history;
    size_t ears[2];
    size_t scratch;
    size_t responses[4];
    size_t size;
    bool overflow;
} Parts;

static Parts LayOut(uint32_t reach, uint32_t taps, uint32_t maxFrames)
{
    maudLayout layout = {.size = sizeof(maudBinaural)};
    Parts parts = {0};
    size_t f = sizeof(float);
    parts.history = maudLayoutAdd(&layout, (size_t)reach + maxFrames, f, alignof(float));
    for (int ear = 0; ear < 2; ++ear)
    {
        parts.ears[ear] = maudLayoutAdd(&layout, (size_t)taps - 1 + maxFrames, f, alignof(float));
    }
    parts.scratch = maudLayoutAdd(&layout, maxFrames, f, alignof(float));
    for (int i = 0; i < 4; ++i)
    {
        parts.responses[i] = maudLayoutAdd(&layout, taps, f, alignof(float));
    }
    parts.size = layout.size;
    parts.overflow = layout.overflow;
    return parts;
}

maudResult maudCreateBinaural(const maudBinauralDef* def, maudBinaural** effectOut)
{
    if (effectOut != nullptr)
    {
        *effectOut = nullptr;
    }
    if (def == nullptr || effectOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    const maudHrtf* hrtf = def->hrtf;
    uint32_t reach = Reach(hrtf);
    Parts parts = LayOut(reach, hrtf->taps, def->maxFrames);
    unsigned char* block =
        parts.overflow ? nullptr : maudAllocate(&def->allocator, parts.size, alignof(maudBinaural));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    memset(block, 0, parts.size);
    maudBinaural* effect = (maudBinaural*)block;
    double fade = round(FADE_SECONDS * (double)hrtf->sampleRate);
    *effect = (maudBinaural){
        .allocator = def->allocator,
        .bytes = parts.size,
        .hrtf = hrtf,
        .maxFrames = def->maxFrames,
        .taps = hrtf->taps,
        .fadeFrames = fade < 1.0 ? 1u : (uint32_t)fade,
        .reach = reach,
        .nearField = def->nearField,
        .headRadius = def->headRadius,
        .history = (float*)(block + parts.history),
        .ears = {(float*)(block + parts.ears[0]), (float*)(block + parts.ears[1])},
        .scratch = (float*)(block + parts.scratch),
        .current = {.response = {(float*)(block + parts.responses[0]),
                                 (float*)(block + parts.responses[1])},
                    .delay = {0.0f, 0.0f}},
        .next = {.response = {(float*)(block + parts.responses[2]),
                              (float*)(block + parts.responses[3])},
                 .delay = {0.0f, 0.0f}},
    };
    *effectOut = effect;
    return maud_success;
}

void maudDestroyBinaural(maudBinaural* effect)
{
    if (effect == nullptr)
    {
        return;
    }
    maudAllocator allocator = effect->allocator;
    maudRelease(&allocator, effect, effect->bytes, alignof(maudBinaural));
}

maudResult maudResetBinaural(maudBinaural* effect)
{
    if (effect == nullptr)
    {
        return maud_errorInvalid;
    }
    memset(effect->history, 0, ((size_t)effect->reach + effect->maxFrames) * sizeof(float));
    for (int ear = 0; ear < 2; ++ear)
    {
        memset(effect->ears[ear], 0,
               ((size_t)effect->taps - 1 + effect->maxFrames) * sizeof(float));
    }
    memset(effect->shelfState, 0, sizeof(effect->shelfState));
    effect->fadeLeft = 0;
    effect->primed = false;
    return maud_success;
}

// The direction as a unit vector; straight ahead for a zero vector.
static maudVector3 Unit(maudVector3 v)
{
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0.0f)
    {
        return (maudVector3){0.0f, 0.0f, -1.0f};
    }
    return (maudVector3){v.x / length, v.y / length, v.z / length};
}

// What the filters depend on: the position, no nearer than MIN_DISTANCE,
// with the near field; the direction alone without it.
static maudVector3 Key(const maudBinaural* effect, maudVector3 position)
{
    maudVector3 unit = Unit(position);
    if (!effect->nearField)
    {
        return unit;
    }
    float length =
        sqrtf(position.x * position.x + position.y * position.y + position.z * position.z);
    float distance = length < MIN_DISTANCE ? MIN_DISTANCE : length;
    return (maudVector3){unit.x * distance, unit.y * distance, unit.z * distance};
}

static bool Same(maudVector3 a, maudVector3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Fills one ear of a filter for a key: the direction the ear looks its
// response up at (with parallax under the near field), converted from
// the listener's frame to the set's (azimuth counterclockwise from
// ahead, -z; left is -x; up is +y), and its near-field filter.
static void DesignEar(const maudBinaural* effect, Filter* filter, uint32_t ear, maudVector3 key)
{
    const maudHrtf* hrtf = effect->hrtf;
    float earX = ear == 0 ? -effect->headRadius : effect->headRadius;
    maudVector3 look = effect->nearField ? maudEarDirection(key, earX, hrtf->distance) : key;
    float azimuth = atan2f(-look.x, -look.z) * DEGREES;
    float y = look.y > 1.0f ? 1.0f : look.y < -1.0f ? -1.0f : look.y;
    maudHrtfNeighbours neighbours = maudHrtfNeighboursOf(hrtf, azimuth, asinf(y) * DEGREES);
    filter->delay[ear] = maudHrtfBlend(hrtf, &neighbours, ear, filter->response[ear]) + BASE_DELAY;
    maudNearFieldFilter shelf = {1.0f, 0.0f, 0.0f};
    if (effect->nearField)
    {
        maudVector3 unit = Unit(key);
        float facing = ear == 0 ? -unit.x : unit.x;
        facing = facing > 1.0f ? 1.0f : facing < -1.0f ? -1.0f : facing;
        float distance = sqrtf(key.x * key.x + key.y * key.y + key.z * key.z);
        shelf = maudNearField(acosf(facing) * DEGREES, effect->headRadius / distance,
                              effect->headRadius / hrtf->distance, effect->headRadius,
                              (float)hrtf->sampleRate);
    }
    filter->shelf[ear][0] = shelf.b0;
    filter->shelf[ear][1] = shelf.b1;
    filter->shelf[ear][2] = shelf.a1;
}

static void Design(const maudBinaural* effect, Filter* filter, maudVector3 key)
{
    DesignEar(effect, filter, 0, key);
    DesignEar(effect, filter, 1, key);
}

// Renders span frames from done with the current filter alone.
static void RenderSteady(maudBinaural* effect, const float* x, float* const out[2], uint32_t done,
                         uint32_t span)
{
    for (uint32_t ear = 0; ear < 2; ++ear)
    {
        float* signal = effect->ears[ear] + effect->taps - 1 + done;
        maudReadDelayed(x, effect->current.delay[ear], 0.0f, signal, span);
        if (effect->nearField)
        {
            static const float still[3] = {0.0f, 0.0f, 0.0f};
            maudShelve(signal, span, effect->current.shelf[ear], still, effect->shelfState[ear]);
        }
        maudFir(signal, effect->current.response[ear], effect->taps, out[ear] + done, span);
    }
}

// Renders span frames from done inside the fade: the delays and the
// near-field filters move, both responses run, and the new one's share
// rises by 1 / fadeFrames a frame, reaching 1 on the fade's last frame.
static void RenderFade(maudBinaural* effect, const float* x, float* const out[2], uint32_t done,
                       uint32_t span)
{
    float fade = (float)effect->fadeFrames;
    uint32_t faded = effect->fadeFrames - effect->fadeLeft;
    for (uint32_t ear = 0; ear < 2; ++ear)
    {
        float from = effect->current.delay[ear];
        float step = (effect->next.delay[ear] - from) / fade;
        float* signal = effect->ears[ear] + effect->taps - 1 + done;
        maudReadDelayed(x, from + step * (float)(faded + 1), step, signal, span);
        if (effect->nearField)
        {
            float start[3];
            float steps[3];
            for (int k = 0; k < 3; ++k)
            {
                steps[k] = (effect->next.shelf[ear][k] - effect->current.shelf[ear][k]) / fade;
                start[k] = effect->current.shelf[ear][k] + steps[k] * (float)(faded + 1);
            }
            maudShelve(signal, span, start, steps, effect->shelfState[ear]);
        }
        float* old = out[ear] + done;
        maudFir(signal, effect->current.response[ear], effect->taps, old, span);
        maudFir(signal, effect->next.response[ear], effect->taps, effect->scratch, span);
        for (uint32_t n = 0; n < span; ++n)
        {
            float share = (float)(faded + n + 1) / fade;
            old[n] += share * (effect->scratch[n] - old[n]);
        }
    }
}

// Renders the call's frames in spans: a fade where one is under way or a
// new direction waits, the current filter alone otherwise.
static void Render(maudBinaural* effect, const float* x, float* const out[2], uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames)
    {
        if (effect->fadeLeft == 0 && !Same(effect->wantedKey, effect->currentKey))
        {
            Design(effect, &effect->next, effect->wantedKey);
            effect->nextKey = effect->wantedKey;
            effect->fadeLeft = effect->fadeFrames;
        }
        if (effect->fadeLeft == 0)
        {
            RenderSteady(effect, x + done, out, done, frames - done);
            return;
        }
        uint32_t span = effect->fadeLeft < frames - done ? effect->fadeLeft : frames - done;
        RenderFade(effect, x + done, out, done, span);
        effect->fadeLeft -= span;
        done += span;
        if (effect->fadeLeft == 0)
        {
            Filter finished = effect->current;
            effect->current = effect->next;
            effect->next = finished;
            effect->currentKey = effect->nextKey;
        }
    }
}

static bool ParamsValid(const maudBinauralParams* params)
{
    return isfinite(params->position.x) && isfinite(params->position.y) &&
           isfinite(params->position.z) && isfinite(params->gain);
}

maudResult maudProcessBinaural(maudBinaural* effect, const maudBinauralParams* params,
                               const float* in, float* const out[2], uint32_t frames)
{
    if (effect == nullptr || params == nullptr || out == nullptr || frames > effect->maxFrames ||
        !ParamsValid(params) ||
        (frames > 0 && (in == nullptr || out[0] == nullptr || out[1] == nullptr)))
    {
        return maud_errorInvalid;
    }
    if (frames == 0)
    {
        return maud_success;
    }
    maudVector3 key = Key(effect, params->position);
    if (!effect->primed)
    {
        Design(effect, &effect->current, key);
        effect->currentKey = key;
        effect->gain = params->gain;
        effect->primed = true;
    }
    effect->wantedKey = key;
    float* x = effect->history + effect->reach;
    memcpy(x, in, (size_t)frames * sizeof(float));
    Render(effect, x, out, frames);
    float from = effect->gain;
    float step = frames > 0 ? (params->gain - from) / (float)frames : 0.0f;
    for (uint32_t ear = 0; ear < 2; ++ear)
    {
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[ear][n] *= from + step * (float)(n + 1);
        }
    }
    effect->gain = params->gain;
    memmove(effect->history, effect->history + frames, (size_t)effect->reach * sizeof(float));
    for (uint32_t ear = 0; ear < 2; ++ear)
    {
        memmove(effect->ears[ear], effect->ears[ear] + frames,
                ((size_t)effect->taps - 1) * sizeof(float));
    }
    return maud_success;
}
