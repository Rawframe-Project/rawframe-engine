// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The noise suppressor (maul-audio/voice.h). A high-pass filter first
// (a Butterworth biquad, as WebRTC's processing runs one ahead of its
// suppressor); then frames of 20 ms under a square-root Hann window,
// 10 ms apart, zero-padded to a power of two. The noise's spectrum
// follows Gerkmann and Hendriks' unbiased MMSE estimate under a speech
// presence probability with a fixed a priori SNR of 15 dB ("Unbiased
// MMSE-based noise power estimation with low complexity and low
// tracking delay", IEEE Trans. ASLP 20(4), 2012), which follows changing
// noise within tens of milliseconds where a minimum search takes a
// second. Each frequency's gain is Cohen's optimally
// modified log-spectral amplitude estimate under that probability
// ("Optimal speech enhancement under signal presence uncertainty using
// log-spectral amplitude estimator", IEEE SPL 9(4), 2002). The channels'
// mean power sets one gain per bin for all of them, so a stereo image
// keeps its place. The output lags the input by 10 ms.

#include "allocator.h"
#include "real_fft.h"
#include "voice_dsp.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <string.h>

#define NOISE_DEF_COOKIE 0x6D616E73u
#define MIN_RATE         8000u
#define MAX_RATE         384000u
#define MIN_FLOOR_DB     (-40.0f)
#define MAX_FLOOR_DB     (-6.0f)
#define MIN_HIGH_PASS    20.0f
#define MAX_HIGH_PASS    400.0f
#define PI               3.14159265358979323846
// OM-LSA's decision-directed weight and a priori SNR floor (Cohen
// 2002); the noise estimate's a priori SNR under speech (15 dB), its
// smoothing per 10 ms and its stagnation guard (Gerkmann and Hendriks
// 2012; the smoothing measured for 10 ms hops).
#define ALPHA_XI    0.92
#define XI_MIN      0.003162
#define XI_SPEECH   31.622776601683793
#define ALPHA_NOISE 0.7
#define ALPHA_GUARD 0.9
#define GUARD       0.99

// A biquad's coefficients, in double, and one channel's state.
typedef struct Biquad
{
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
} Biquad;

typedef struct BiquadState
{
    double x1;
    double x2;
    double y1;
    double y2;
} BiquadState;

struct maudNoiseSuppressor
{
    maudAllocator allocator;
    uint32_t channels;
    // The hop (10 ms of frames), the transform's size and its bins.
    uint32_t hop;
    uint32_t size;
    uint32_t bins;
    // How far into the hop the stream is.
    uint32_t position;
    double floor;
    bool highPass;
    Biquad filter;
    uint64_t frames;
    maudRealFft fft;
    float* window;
    // Per channel: the high-pass filter's state, the last hop's input,
    // this hop's, the output ready to go and the synthesis tail to add
    // to the next.
    BiquadState* filterState;
    float* previous;
    float* pending;
    float* ready;
    float* tail;
    // Scratch: one channel's frame, every channel's bins and the gains.
    float* frame;
    float* spectrum;
    float* gain;
    // Per bin: the mean power, the noise's estimate, the guard's
    // smoothed probability and OM-LSA's memory of the last frame.
    double* power;
    double* noise;
    double* guard;
    double* lastGain;
    double* lastGamma;
    float speech;
    float noiseDbfs;
    size_t bytes;
};

maudNoiseSuppressorDef maudDefaultNoiseSuppressorDef(void)
{
    return (maudNoiseSuppressorDef){
        .cookie = NOISE_DEF_COOKIE,
        .sampleRate = 48000,
        .layout = maud_layoutMono,
        .floorDb = -30.0f,
        .highPassHz = 100.0f,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static uint32_t PowerOfTwo(uint32_t atLeast)
{
    uint32_t n = 4;
    while (n < atLeast)
    {
        n *= 2;
    }
    return n;
}

static size_t Up(size_t n)
{
    return (n + 15u) & ~(size_t)15u;
}

// Where each part of the object's one allocation starts: the object,
// the transform's tables, the filters' states, the floats, the doubles.
typedef struct Layout
{
    size_t fft;
    size_t filters;
    size_t floats;
    size_t doubles;
    size_t total;
} Layout;

static Layout LayoutOf(uint32_t channels, uint32_t hop, uint32_t size)
{
    uint32_t bins = size / 2 + 1;
    Layout l;
    l.fft = Up(sizeof(struct maudNoiseSuppressor));
    l.filters = l.fft + Up(maudRealFftBytes(size));
    l.floats = l.filters + Up((size_t)channels * sizeof(BiquadState));
    size_t floats =
        2 * (size_t)hop + 4 * (size_t)channels * hop + size + 2 * (size_t)channels * bins + bins;
    l.doubles = l.floats + Up(floats * sizeof(float));
    l.total = l.doubles + 5 * (size_t)bins * sizeof(double);
    return l;
}

// A 2nd-order Butterworth high-pass (R. Bristow-Johnson's cookbook).
static Biquad HighPass(double hz, double rate)
{
    double w = 2.0 * PI * hz / rate;
    double alpha = sin(w) / (2.0 * 0.70710678118654752);
    double c = cos(w);
    double a0 = 1.0 + alpha;
    return (Biquad){(1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0,
                    (1.0 - alpha) / a0};
}

static bool DefValid(const maudNoiseSuppressorDef* def)
{
    return def->cookie == NOISE_DEF_COOKIE && def->sampleRate >= MIN_RATE &&
           def->sampleRate <= MAX_RATE && def->sampleRate % 100 == 0 &&
           def->floorDb >= MIN_FLOOR_DB && def->floorDb <= MAX_FLOOR_DB &&
           (def->highPassHz == 0.0f ||
            (def->highPassHz >= MIN_HIGH_PASS && def->highPassHz <= MAX_HIGH_PASS)) &&
           maudGetLayoutChannelCount(def->layout) > 0 && maudIsAllocatorValid(&def->allocator);
}

static void Place(maudNoiseSuppressor* s, unsigned char* memory, const Layout* l)
{
    size_t perChannel = (size_t)s->channels * s->hop;
    maudInitRealFft(&s->fft, s->size, memory + l->fft);
    s->filterState = (BiquadState*)(memory + l->filters);
    float* f = (float*)(memory + l->floats);
    float** floats[8] = {&s->window, &s->previous, &s->pending,  &s->ready,
                         &s->tail,   &s->frame,    &s->spectrum, &s->gain};
    size_t counts[8] = {2 * (size_t)s->hop,
                        perChannel,
                        perChannel,
                        perChannel,
                        perChannel,
                        s->size,
                        2 * (size_t)s->channels * s->bins,
                        s->bins};
    for (int i = 0; i < 8; ++i)
    {
        *floats[i] = f;
        f += counts[i];
    }
    double* d = (double*)(memory + l->doubles);
    double** doubles[5] = {&s->power, &s->noise, &s->guard, &s->lastGain, &s->lastGamma};
    for (int i = 0; i < 5; ++i)
    {
        *doubles[i] = d;
        d += s->bins;
    }
}

maudResult maudCreateNoiseSuppressor(const maudNoiseSuppressorDef* def,
                                     maudNoiseSuppressor** suppressorOut)
{
    if (suppressorOut != nullptr)
    {
        *suppressorOut = nullptr;
    }
    if (def == nullptr || suppressorOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    uint32_t channels = maudGetLayoutChannelCount(def->layout);
    uint32_t hop = def->sampleRate / 100;
    uint32_t size = PowerOfTwo(2 * hop);
    Layout l = LayoutOf(channels, hop, size);
    unsigned char* memory = maudAllocate(&def->allocator, l.total, alignof(double));
    if (memory == nullptr)
    {
        return maud_errorCapacity;
    }
    maudNoiseSuppressor* s = (maudNoiseSuppressor*)memory;
    *s = (maudNoiseSuppressor){.allocator = def->allocator,
                               .channels = channels,
                               .hop = hop,
                               .size = size,
                               .bins = size / 2 + 1,
                               .floor = pow(10.0, (double)def->floorDb / 20.0),
                               .highPass = def->highPassHz > 0.0f,
                               .bytes = l.total,
                               .noiseDbfs = -120.0f};
    if (s->highPass)
    {
        s->filter = HighPass((double)def->highPassHz, (double)def->sampleRate);
    }
    Place(s, memory, &l);
    memset(s->filterState, 0, channels * sizeof(BiquadState));
    memset(s->previous, 0, 4 * (size_t)channels * hop * sizeof(float));
    // A periodic square-root Hann window: its squares at a hop's distance
    // add to one, so analysis and synthesis by it reconstruct exactly.
    for (uint32_t n = 0; n < 2 * hop; ++n)
    {
        s->window[n] = (float)sin(PI * ((double)n + 0.5) / (2.0 * hop));
    }
    *suppressorOut = s;
    return maud_success;
}

void maudDestroyNoiseSuppressor(maudNoiseSuppressor* suppressor)
{
    if (suppressor != nullptr)
    {
        maudAllocator allocator = suppressor->allocator;
        maudRelease(&allocator, suppressor, suppressor->bytes, alignof(double));
    }
}

// A bin's speech presence probability, guarded against stagnation.
static double Presence(maudNoiseSuppressor* s, uint32_t k)
{
    double ratio = s->power[k] / (s->noise[k] + 1e-30) * XI_SPEECH / (1.0 + XI_SPEECH);
    double p = 1.0 / (1.0 + (1.0 + XI_SPEECH) * exp(-fmin(ratio, 700.0)));
    s->guard[k] = ALPHA_GUARD * s->guard[k] + (1.0 - ALPHA_GUARD) * p;
    return s->guard[k] > GUARD ? fmin(p, GUARD) : p;
}

// A bin's OM-LSA gain under presence p, against the noise before this
// frame's update.
static float BinGain(maudNoiseSuppressor* s, uint32_t k, double p)
{
    double gamma = s->power[k] / (s->noise[k] + 1e-30);
    double xi = ALPHA_XI * s->lastGain[k] * s->lastGain[k] * s->lastGamma[k] +
                (1.0 - ALPHA_XI) * fmax(gamma - 1.0, 0.0);
    xi = fmax(xi, XI_MIN);
    double v = gamma * xi / (1.0 + xi);
    double gainH1 = fmin(xi / (1.0 + xi) * exp(0.5 * maudExpIntegral(fmax(v, 1e-10))), 1.0);
    s->lastGain[k] = gainH1;
    s->lastGamma[k] = gamma;
    double g = pow(gainH1, p) * pow(s->floor, 1.0 - p);
    return (float)fmin(fmax(g, s->floor), 1.0);
}

// The frame's gains from its mean power, then the noise's update.
static void Gains(maudNoiseSuppressor* s)
{
    double speech = 0.0;
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        if (s->frames == 0)
        {
            s->noise[k] = s->power[k];
            s->guard[k] = 0.5;
            s->lastGain[k] = 1.0;
            s->lastGamma[k] = 1.0;
        }
        double p = Presence(s, k);
        s->gain[k] = BinGain(s, k, p);
        speech += p;
        double expected = (1.0 - p) * s->power[k] + p * s->noise[k];
        s->noise[k] = ALPHA_NOISE * s->noise[k] + (1.0 - ALPHA_NOISE) * expected;
    }
    s->speech = (float)(speech / s->bins);
}

// The noise's level in dBFS from its spectrum: Parseval over the
// window's energy (its squares add to the hop).
static float NoiseDbfs(const maudNoiseSuppressor* s)
{
    double sum = s->noise[0] + s->noise[s->bins - 1];
    for (uint32_t k = 1; k + 1 < s->bins; ++k)
    {
        sum += 2.0 * s->noise[k];
    }
    double meanSquare = sum / ((double)s->size * (double)s->hop);
    return (float)fmax(10.0 * log10(meanSquare + 1e-12), -120.0);
}

// One channel's frame, windowed and transformed into its bins, its power
// added to the mean.
static void Analyse(maudNoiseSuppressor* s, uint32_t c)
{
    uint32_t hop = s->hop;
    const float* previous = s->previous + (size_t)c * hop;
    const float* pending = s->pending + (size_t)c * hop;
    for (uint32_t n = 0; n < hop; ++n)
    {
        s->frame[n] = previous[n] * s->window[n];
        s->frame[hop + n] = pending[n] * s->window[hop + n];
    }
    for (uint32_t n = 2 * hop; n < s->size; ++n)
    {
        s->frame[n] = 0.0f;
    }
    float* bins = s->spectrum + 2 * (size_t)c * s->bins;
    maudForwardRealFft(&s->fft, s->frame, bins);
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        double re = (double)bins[2 * k];
        double im = (double)bins[2 * k + 1];
        s->power[k] += (re * re + im * im) / (double)s->channels;
    }
}

// One channel's bins under the gains, back to samples: the next hop of
// output and the tail for the one after.
static void Synthesise(maudNoiseSuppressor* s, uint32_t c)
{
    uint32_t hop = s->hop;
    float* bins = s->spectrum + 2 * (size_t)c * s->bins;
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        bins[2 * k] *= s->gain[k];
        bins[2 * k + 1] *= s->gain[k];
    }
    maudInverseRealFft(&s->fft, bins, s->frame);
    float* ready = s->ready + (size_t)c * hop;
    float* tail = s->tail + (size_t)c * hop;
    for (uint32_t n = 0; n < hop; ++n)
    {
        ready[n] = tail[n] + s->frame[n] * s->window[n];
        tail[n] = s->frame[hop + n] * s->window[hop + n];
    }
    memcpy(s->previous + (size_t)c * hop, s->pending + (size_t)c * hop, hop * sizeof(float));
}

// A hop is complete: analyse every channel, find the gains, and
// synthesise the next hop of output.
static void Process(maudNoiseSuppressor* s)
{
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        s->power[k] = 0.0;
    }
    for (uint32_t c = 0; c < s->channels; ++c)
    {
        Analyse(s, c);
    }
    Gains(s);
    s->noiseDbfs = NoiseDbfs(s);
    for (uint32_t c = 0; c < s->channels; ++c)
    {
        Synthesise(s, c);
    }
    s->frames += 1;
}

static float Filter(const Biquad* f, BiquadState* z, float in)
{
    double x = (double)in;
    double y = f->b0 * x + f->b1 * z->x1 + f->b2 * z->x2 - f->a1 * z->y1 - f->a2 * z->y2;
    z->x2 = z->x1;
    z->x1 = x;
    z->y2 = z->y1;
    z->y1 = y;
    return (float)y;
}

maudResult maudSuppressNoise(maudNoiseSuppressor* suppressor, float* frames, uint32_t frameCount,
                             maudNoiseState* stateOut)
{
    if (suppressor == nullptr || (frames == nullptr && frameCount > 0))
    {
        return maud_errorInvalid;
    }
    maudNoiseSuppressor* s = suppressor;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        for (uint32_t c = 0; c < s->channels; ++c)
        {
            float* sample = &frames[(size_t)i * s->channels + c];
            size_t at = (size_t)c * s->hop + s->position;
            float in = s->highPass ? Filter(&s->filter, &s->filterState[c], *sample) : *sample;
            *sample = s->ready[at];
            s->pending[at] = in;
        }
        if (++s->position == s->hop)
        {
            s->position = 0;
            Process(s);
        }
    }
    if (stateOut != nullptr)
    {
        *stateOut = (maudNoiseState){s->speech, s->noiseDbfs, s->frames};
    }
    return maud_success;
}
