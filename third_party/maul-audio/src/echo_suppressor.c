// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's suppressor (echo_suppressor.h). Each block makes
// a frame with the one before it. Per bin: the residual echo, the leakage
// times the echo estimate's power, held against a decay of 0.9 a 8 ms
// block; the interference, it and the noise; the a priori SNR by the
// decision-directed estimate, its weight 0.1 + 0.89 (S / (S + N))^2 from
// the last frame's speech level S. Per Bark band (of 16 kHz's 24 up to
// 8 kHz, as many more of their width above): that SNR smoothed. The
// frame's probability of speech, 0.1 + 0.9 q(Z) from the bands' mean
// below 8 kHz, and each bin's a priori absence, 1 - P(frame) (0.199 +
// 0.8 q(band)), with q(x) = 1 / (1 + 0.15 / x), give its presence; the
// gain is the log-spectral amplitude estimate to the power of that
// presence times the floor to the rest, the floor the noise's and the
// echo's mixed by their powers, the echo's moved from -40 to -15 dB by
// the frame's probability.

#include "echo_suppressor.h"

#include "allocator.h"
#include "voice_dsp.h"

#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846
// The block's length the measured constants were set for, in seconds.
#define MEASURED_BLOCK (128.0 / 16000.0)
// The bands below 8 kHz, and the a priori SNR's floor.
#define SPEECH_BANDS 24u
#define XI_MIN       0.003162
// The echo floor at the frame probability's two ends, in dB.
#define ECHO_FLOOR_QUIET  (-40.0)
#define ECHO_FLOOR_SPEECH (-15.0)

static double Bark(double hz)
{
    return 13.0 * atan(0.00076 * hz) + 3.5 * atan((hz / 7500.0) * (hz / 7500.0));
}

// The bands: 24 to 8 kHz, as many more of their width to the Nyquist.
static uint32_t BandsOf(double sampleRate)
{
    uint32_t bands = (uint32_t)ceil(Bark(sampleRate / 2.0) / Bark(8000.0) * SPEECH_BANDS - 1e-9);
    return bands > SPEECH_BANDS ? bands : SPEECH_BANDS;
}

typedef struct Places
{
    uint32_t bins;
    uint32_t bands;
    size_t doubles;
    size_t floats;
    size_t words;
    size_t halves;
} Places;

static Places PlacesOf(uint32_t block, double sampleRate)
{
    Places p = {.bins = block + 1, .bands = BandsOf(sampleRate)};
    p.doubles = 7 * (size_t)p.bins + 2 * (size_t)p.bands;
    // The window, the last blocks, the tail, a frame and two spectra.
    p.floats = 2 * (size_t)block + 3 * (size_t)block + 2 * (size_t)block + 4 * (size_t)p.bins;
    p.words = p.bands;
    p.halves = p.bins;
    return p;
}

size_t maudEchoSuppressorBytes(uint32_t block, double sampleRate)
{
    Places p = PlacesOf(block, sampleRate);
    maudLayout layout = {0};
    (void)maudLayoutAdd(&layout, p.doubles, sizeof(double), alignof(double));
    (void)maudLayoutAdd(&layout, p.floats, sizeof(float), alignof(float));
    (void)maudLayoutAdd(&layout, p.words, sizeof(uint32_t), alignof(uint32_t));
    (void)maudLayoutAdd(&layout, p.halves, sizeof(uint16_t), alignof(uint16_t));
    return layout.overflow ? 0 : layout.size;
}

// Each bin's band, and the bins per band.
static void Bands(maudEchoSuppressor* s, double sampleRate)
{
    double top = Bark(8000.0);
    memset(s->perBand, 0, s->bands * sizeof(uint32_t));
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        double hz = (double)k * sampleRate / (2.0 * s->block);
        double at = floor(Bark(hz) / top * SPEECH_BANDS);
        uint32_t b = (uint32_t)fmin((double)(s->bands - 1), at);
        s->band[k] = (uint16_t)b;
        s->perBand[b] += 1;
    }
}

void maudInitEchoSuppressor(maudEchoSuppressor* suppressor, uint32_t block, double sampleRate,
                            double floorDb, const maudRealFft* fft, void* memory)
{
    Places p = PlacesOf(block, sampleRate);
    double* d = memory;
    float* f = (float*)(d + p.doubles);
    uint32_t* w = (uint32_t*)(f + p.floats);
    double scale = ((double)block / sampleRate) / MEASURED_BLOCK;
    size_t n = block;
    *suppressor = (maudEchoSuppressor){
        .block = block,
        .bins = p.bins,
        .bands = p.bands,
        .speechBands = SPEECH_BANDS,
        .perBand = w,
        .band = (uint16_t*)(w + p.words),
        .fft = fft,
        .window = f,
        .lastOutput = f + 2 * n,
        .lastEcho = f + 3 * n,
        .tail = f + 4 * n,
        .frame = f + 5 * n,
        .spectrum = f + 7 * n,
        .echoSpectrum = f + 7 * n + 2 * (size_t)p.bins,
        .residual = d,
        .noise = d + p.bins,
        .lastSpeech = d + 2 * (size_t)p.bins,
        .power = d + 3 * (size_t)p.bins,
        .interference = d + 4 * (size_t)p.bins,
        .posterior = d + 5 * (size_t)p.bins,
        .prior = d + 6 * (size_t)p.bins,
        .zeta = d + 7 * (size_t)p.bins,
        .sum = d + 7 * (size_t)p.bins + p.bands,
        .noiseFloor = pow(10.0, floorDb / 10.0),
        .echoFloorQuiet = pow(10.0, ECHO_FLOOR_QUIET / 10.0),
        .echoFloorSpeech = pow(10.0, ECHO_FLOOR_SPEECH / 10.0),
        .residualDecay = pow(0.9, scale),
        .zetaKeep = pow(0.7, scale),
        .speechKeep = pow(0.2, scale),
        .noiseKeep = pow(0.7, scale),
    };
    for (uint32_t i = 0; i < 2 * block; ++i)
    {
        suppressor->window[i] = (float)sin(PI_D * ((double)i + 0.5) / (2.0 * block));
    }
    memset(f + 2 * n, 0, 3 * n * sizeof(float));
    memset(d, 0, p.doubles * sizeof(double));
    Bands(suppressor, sampleRate);
}

// A frame's windowed spectrum: the last block, then this one.
static void Analyse(maudEchoSuppressor* s, const float* last, const float* now, float* spectrum)
{
    for (uint32_t i = 0; i < s->block; ++i)
    {
        s->frame[i] = last[i] * s->window[i];
        s->frame[s->block + i] = now[i] * s->window[s->block + i];
    }
    maudForwardRealFft(s->fft, s->frame, spectrum);
}

// Per bin: the power, the residual echo, the interference and the SNRs;
// the first frame sets the noise to its power.
static void Measure(maudEchoSuppressor* s, const float* bandLeak, uint32_t bandBins)
{
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        const float* e = s->spectrum + 2 * k;
        const float* y = s->echoSpectrum + 2 * k;
        double e2 = (double)e[0] * (double)e[0] + (double)e[1] * (double)e[1];
        double y2 = (double)y[0] * (double)y[0] + (double)y[1] * (double)y[1];
        double leak = (double)bandLeak[k / bandBins];
        s->residual[k] = fmax(leak * y2, s->residualDecay * s->residual[k]);
        if (!s->started)
        {
            s->noise[k] = e2;
            s->lastSpeech[k] = e2;
        }
        double lambda = s->noise[k] + s->residual[k] + 1e-20;
        double gamma = e2 / lambda;
        double old = s->lastSpeech[k];
        double share = old / (old + lambda);
        double weight = 0.1 + 0.89 * share * share;
        s->power[k] = e2;
        s->interference[k] = lambda;
        s->posterior[k] = gamma;
        s->prior[k] = fmax(weight * fmax(gamma - 1.0, 0.0) + (1.0 - weight) * old / lambda, XI_MIN);
    }
}

// The bands' smoothed a priori SNRs, and the frame's probability of
// speech from those below 8 kHz.
static void Frame(maudEchoSuppressor* s)
{
    memset(s->sum, 0, s->bands * sizeof(double));
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        s->sum[s->band[k]] += s->prior[k];
    }
    double mean = 0.0;
    uint32_t counted = 0;
    for (uint32_t b = 0; b < s->bands; ++b)
    {
        if (s->perBand[b] == 0)
        {
            continue;
        }
        double now = s->sum[b] / s->perBand[b];
        s->zeta[b] = s->started ? s->zetaKeep * s->zeta[b] + (1.0 - s->zetaKeep) * now : now;
        if (b < s->speechBands)
        {
            mean += s->zeta[b];
            counted += 1;
        }
    }
    mean /= counted > 0 ? counted : 1;
    s->frameProbability = 0.1 + 0.9 / (1.0 + 0.15 / (mean + 1e-12));
}

// A bin's gain: its presence, the log-spectral amplitude estimate, the
// floor; and the noise's and the last speech level's update.
static float Gain(maudEchoSuppressor* s, uint32_t k, double echoFloor)
{
    double xi = s->prior[k];
    double v = s->posterior[k] * xi / (1.0 + xi);
    double p1 = 0.199 + 0.8 / (1.0 + 0.15 / (s->zeta[s->band[k]] + 1e-12));
    double q = fmin(fmax(1.0 - s->frameProbability * p1, 1e-6), 1.0 - 1e-6);
    double p = 1.0 / (1.0 + q / (1.0 - q) * (1.0 + xi) * exp(-fmin(v, 700.0)));
    double gain = fmin(xi / (1.0 + xi) * exp(0.5 * maudExpIntegral(fmax(v, 1e-10))), 1.0);
    double e2 = s->power[k];
    s->lastSpeech[k] = s->speechKeep * s->lastSpeech[k] + (1.0 - s->speechKeep) * gain * gain * e2;
    double noise = s->noise[k];
    double echo = s->residual[k];
    double floor = sqrt((s->noiseFloor * noise + echoFloor * echo) / (noise + echo + 1e-20));
    // gain^p floor^(1 - p), in one exponential.
    double g = exp(p * log(fmax(gain, 1e-30)) + (1.0 - p) * log(fmax(floor, 1e-30)));
    g = fmin(fmax(g, floor), 1.0);
    double expected = (1.0 - p) * fmax(e2 - echo, 0.0) + p * noise;
    s->noise[k] = s->noiseKeep * noise + (1.0 - s->noiseKeep) * expected;
    return (float)g;
}

void maudRunEchoSuppressor(maudEchoSuppressor* s, const float* output, const float* echo,
                           const float* bandLeak, uint32_t bandBins, float* out)
{
    uint32_t n = s->block;
    if (!s->primed)
    {
        // A frame takes two blocks: the first block out is silence.
        memcpy(s->lastOutput, output, n * sizeof(float));
        memcpy(s->lastEcho, echo, n * sizeof(float));
        memset(out, 0, n * sizeof(float));
        s->primed = true;
        return;
    }
    Analyse(s, s->lastOutput, output, s->spectrum);
    Analyse(s, s->lastEcho, echo, s->echoSpectrum);
    memcpy(s->lastOutput, output, n * sizeof(float));
    memcpy(s->lastEcho, echo, n * sizeof(float));
    Measure(s, bandLeak, bandBins);
    Frame(s);
    double p = s->frameProbability;
    double echoFloor = pow(10.0, ((1.0 - p) * ECHO_FLOOR_QUIET + p * ECHO_FLOOR_SPEECH) / 10.0);
    for (uint32_t k = 0; k < s->bins; ++k)
    {
        float g = Gain(s, k, echoFloor);
        s->spectrum[2 * k] *= g;
        s->spectrum[2 * k + 1] *= g;
    }
    s->started = true;
    maudInverseRealFft(s->fft, s->spectrum, s->frame);
    for (uint32_t i = 0; i < n; ++i)
    {
        out[i] = s->tail[i] + s->frame[i] * s->window[i];
        s->tail[i] = s->frame[n + i] * s->window[n + i];
    }
}
