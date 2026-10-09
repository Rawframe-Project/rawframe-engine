// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's linear stage (echo_filter.h). Each block: the render's newest spectrum joins
// the partitions; both filters estimate the echo; the two paths' test compares their errors; the
// leakage (the regression of the error's power on the echo estimate's, on their changes) sets each
// bin's rate as min(leak |Y|^2 / |E|^2, 0.5) blended with the residual-to-error ratio of the block
// (Valin 2007), or, until the filter has learnt a partition's worth, a plain rate from the render's
// energy against the error's; the background adapts with steps proportionate to each partition's
// share of the filter, and one partition a block in turn is constrained to a linear convolution.

#include "echo_filter.h"

#include "allocator.h"

#include <math.h>
#include <string.h>

// The highest rate, the leakage's and the residual-to-error ratio's
// shares in it, and the start rate.
#define MU_MAX     0.5
#define LEAK_SHARE 0.7
#define RER_SHARE  0.3
#define START_RATE 0.25
// The leakage's floor (as a share of the echo) and its first value.
#define LEAK_FLOOR 0.005
#define LEAK_START 0.25
// The step's power floor, as a share of the render's mean power.
#define POWER_FLOOR 0.01
// The block's length the measured constants were set for, in seconds.
#define MEASURED_BLOCK (128.0 / 16000.0)

typedef struct Sums
{
    // The background's and the foreground's errors' energies, and the
    // background's echo estimate's.
    double background;
    double foreground;
    double echo;
} Sums;

static float* Partition(float* spectra, uint32_t bins, uint32_t index)
{
    return spectra + (size_t)index * 2 * bins;
}

// Where each array starts, in floats from the block's start for the
// floats and in doubles from the first double for the doubles.
typedef struct Places
{
    uint32_t bins;
    uint32_t perBand;
    uint32_t bands;
    size_t spectra;
    size_t floats;
    size_t doubles;
} Places;

static Places PlacesOf(uint32_t block, uint32_t partitions, double sampleRate)
{
    Places p = {.bins = block + 1};
    p.perBand = (uint32_t)fmax(1.0, round(MAUD_ECHO_BAND_HZ * 2.0 * block / sampleRate));
    p.bands = (p.bins + p.perBand - 1) / p.perBand;
    p.spectra = (size_t)partitions * 2 * p.bins;
    // Three filters' spectra; the last block, twice a block of time, three
    // spectra of bins, two estimates of twice a block and an error; and
    // the band leakages, as floats rounded up to a double.
    p.floats = 3 * p.spectra + 8 * (size_t)block + 6 * (size_t)p.bins + 2 * ((p.bands + 1) / 2);
    p.doubles = 3 * (size_t)p.bins + 4 * (size_t)p.bands + partitions;
    return p;
}

size_t maudEchoFilterBytes(uint32_t block, uint32_t partitions, double sampleRate)
{
    Places p = PlacesOf(block, partitions, sampleRate);
    maudLayout layout = {0};
    (void)maudLayoutAdd(&layout, p.doubles, sizeof(double), alignof(double));
    (void)maudLayoutAdd(&layout, p.floats, sizeof(float), alignof(double));
    return layout.overflow ? 0 : layout.size;
}

void maudInitEchoFilter(maudEchoFilter* filter, uint32_t block, uint32_t partitions,
                        double sampleRate, const maudRealFft* fft, void* memory)
{
    Places p = PlacesOf(block, partitions, sampleRate);
    double* d = memory;
    float* f = (float*)(d + p.doubles);
    float* scratch = f + 3 * p.spectra;
    double scale = ((double)block / sampleRate) / MEASURED_BLOCK;
    *filter = (maudEchoFilter){
        .block = block,
        .bins = p.bins,
        .partitions = partitions,
        .bands = p.bands,
        .binsPerBand = p.perBand,
        .fft = fft,
        .render = f,
        .background = f + p.spectra,
        .foreground = f + 2 * p.spectra,
        .last = scratch,
        .time = scratch + block,
        .backgroundEcho = scratch + 3 * (size_t)block,
        .foregroundEcho = scratch + 5 * (size_t)block,
        .backgroundError = scratch + 7 * (size_t)block,
        .spectrum = scratch + 8 * (size_t)block,
        .errorSpectrum = scratch + 8 * (size_t)block + 2 * (size_t)p.bins,
        .echoSpectrum = scratch + 8 * (size_t)block + 4 * (size_t)p.bins,
        .bandLeak = scratch + 8 * (size_t)block + 6 * (size_t)p.bins,
        .slowPower = d,
        .errorRecent = d + p.bins,
        .echoRecent = d + 2 * (size_t)p.bins,
        .bandNow = d + 3 * (size_t)p.bins,
        .bandCovariance = d + 3 * (size_t)p.bins + 2 * (size_t)p.bands,
        .bandVariance = d + 3 * (size_t)p.bins + 3 * (size_t)p.bands,
        .shares = d + 3 * (size_t)p.bins + 4 * (size_t)p.bands,
        .recent = pow(0.65, scale),
        .window1 = pow(0.6, scale),
        .window2 = pow(0.85, scale),
    };
    maudResetEchoFilter(filter);
}

void maudResetEchoFilter(maudEchoFilter* f)
{
    size_t spectra = (size_t)f->partitions * 2 * f->bins;
    memset(f->render, 0, 3 * spectra * sizeof(float));
    memset(f->last, 0, f->block * sizeof(float));
    memset(f->slowPower, 0, (3 * (size_t)f->bins + 4 * (size_t)f->bands) * sizeof(double));
    for (uint32_t q = 0; q < f->bands; ++q)
    {
        f->bandLeak[q] = (float)LEAK_START;
    }
    f->head = 0;
    f->leak = LEAK_START;
    f->covariance = 0.0;
    f->variance = 1.0;
    f->drop1 = f->drop2 = f->noise1 = f->noise2 = 0.0;
    f->started = 0.0;
    f->adapted = false;
    f->constrain = 0;
}

// The spectrum of a block after zeros (a block of zeros, then x).
static void Padded(const maudEchoFilter* f, const float* x, float* spectrum)
{
    memset(f->time, 0, f->block * sizeof(float));
    memcpy(f->time + f->block, x, f->block * sizeof(float));
    maudForwardRealFft(f->fft, f->time, spectrum);
}

// The newest render spectrum, of the last block and this one.
static void Push(maudEchoFilter* f, const float* render)
{
    memcpy(f->time, f->last, f->block * sizeof(float));
    memcpy(f->time + f->block, render, f->block * sizeof(float));
    memcpy(f->last, render, f->block * sizeof(float));
    f->head = (f->head + f->partitions - 1) % f->partitions;
    maudForwardRealFft(f->fft, f->time, Partition(f->render, f->bins, f->head));
}

// A filter's echo estimate's spectrum, the partitions' products summed.
static void Estimate(const maudEchoFilter* f, const float* filter, float* out)
{
    memset(out, 0, 2 * (size_t)f->bins * sizeof(float));
    for (uint32_t m = 0; m < f->partitions; ++m)
    {
        const float* x = Partition(f->render, f->bins, (f->head + m) % f->partitions);
        const float* w = filter + (size_t)m * 2 * f->bins;
        for (uint32_t k = 0; k < f->bins; ++k)
        {
            float xr = x[2 * k];
            float xi = x[2 * k + 1];
            out[2 * k] += xr * w[2 * k] - xi * w[2 * k + 1];
            out[2 * k + 1] += xr * w[2 * k + 1] + xi * w[2 * k];
        }
    }
}

// The two paths' test: the error's drop from foreground
// to background against the noise of that comparison, now and over two
// windows. The foreground takes the background's filter when it is
// significantly better; the background starts again from the
// foreground when it is four times as significantly worse, and its
// error and estimate become the foreground's.
static void Paths(maudEchoFilter* f, Sums* s, const float* yb, const float* yf, float* eb,
                  const float* ef, float* ybOut)
{
    double differ = 1e-10;
    for (uint32_t i = 0; i < f->block; ++i)
    {
        double d = (double)(yf[i] - yb[i]);
        differ += d * d;
    }
    double drop = s->foreground - s->background;
    double w1 = f->window1;
    double w2 = f->window2;
    f->drop1 = w1 * f->drop1 + (1.0 - w1) * drop;
    f->drop2 = w2 * f->drop2 + (1.0 - w2) * drop;
    f->noise1 = w1 * w1 * f->noise1 + (1.0 - w1) * (1.0 - w1) * s->foreground * differ;
    f->noise2 = w2 * w2 * f->noise2 + (1.0 - w2) * (1.0 - w2) * s->foreground * differ;
    size_t bytes = (size_t)f->partitions * 2 * f->bins * sizeof(float);
    if (drop * fabs(drop) > s->foreground * differ || f->drop1 * fabs(f->drop1) > 0.5 * f->noise1 ||
        f->drop2 * fabs(f->drop2) > 0.25 * f->noise2)
    {
        memcpy(f->foreground, f->background, bytes);
        f->drop1 = f->drop2 = f->noise1 = f->noise2 = 0.0;
    }
    else if (-drop * fabs(drop) > 4.0 * s->foreground * differ ||
             -f->drop1 * fabs(f->drop1) > 4.0 * f->noise1 ||
             -f->drop2 * fabs(f->drop2) > 4.0 * f->noise2)
    {
        memcpy(f->background, f->foreground, bytes);
        memcpy(eb, ef, f->block * sizeof(float));
        memcpy(ybOut, yf, f->block * sizeof(float));
        s->background = s->foreground;
        f->drop1 = f->drop2 = f->noise1 = f->noise2 = 0.0;
    }
}

// One band's leakage from its regression's sums this block.
static void BandLeak(maudEchoFilter* f, uint32_t q, double covariance, double variance,
                     double alpha)
{
    if (variance <= 0.0)
    {
        f->bandLeak[q] = q > 0 ? f->bandLeak[q - 1] : (float)f->leak;
        return;
    }
    double a = fmin(alpha * 2.0, 0.3);
    f->bandCovariance[q] = (1.0 - a) * f->bandCovariance[q] + a * covariance / sqrt(variance);
    f->bandVariance[q] = (1.0 - a) * f->bandVariance[q] + a * sqrt(variance);
    double l = f->bandVariance[q] > 1e-15 ? f->bandCovariance[q] / f->bandVariance[q] : f->leak;
    f->bandLeak[q] = (float)fmin(fmax(l, LEAK_FLOOR), 1.0);
}

// The leakage, from the background's error and echo estimate spectra:
// over all bins and per band.
static void Leakage(maudEchoFilter* f, const Sums* s)
{
    double covariance = 0.0;
    double variance = 0.0;
    double alpha = fmin(0.2 * s->echo / (s->background + 1e-12), 0.2);
    double keep = f->recent;
    for (uint32_t q = 0; q < f->bands; ++q)
    {
        double bandCovariance = 0.0;
        double bandVariance = 0.0;
        uint32_t end = (q + 1) * f->binsPerBand < f->bins ? (q + 1) * f->binsPerBand : f->bins;
        for (uint32_t k = q * f->binsPerBand; k < end; ++k)
        {
            const float* e = f->errorSpectrum + 2 * k;
            const float* y = f->echoSpectrum + 2 * k;
            double e2 = (double)e[0] * (double)e[0] + (double)e[1] * (double)e[1];
            double y2 = (double)y[0] * (double)y[0] + (double)y[1] * (double)y[1];
            double eh = e2 - f->errorRecent[k];
            double yh = y2 - f->echoRecent[k];
            covariance += eh * yh;
            variance += yh * yh;
            bandCovariance += eh * yh;
            bandVariance += yh * yh;
            f->errorRecent[k] = keep * f->errorRecent[k] + (1.0 - keep) * e2;
            f->echoRecent[k] = keep * f->echoRecent[k] + (1.0 - keep) * y2;
        }
        f->bandNow[2 * q] = bandCovariance;
        f->bandNow[2 * q + 1] = bandVariance;
    }
    if (variance > 0.0)
    {
        f->covariance = (1.0 - alpha) * f->covariance + alpha * covariance / sqrt(variance);
        f->variance = fmax((1.0 - alpha) * f->variance + alpha * sqrt(variance), 1e-12);
        f->covariance = fmin(fmax(f->covariance, LEAK_FLOOR * f->variance), f->variance);
        f->leak = f->covariance / f->variance;
    }
    for (uint32_t q = 0; q < f->bands; ++q)
    {
        BandLeak(f, q, f->bandNow[2 * q], f->bandNow[2 * q + 1], alpha);
    }
}

// Each partition's share of the step: its share of the filter's energy
// plus a tenth of the largest share, summing to 0.99; uniform until the
// filter has adapted.
static void Shares(maudEchoFilter* f)
{
    double largest = 0.0;
    double sum = 0.0;
    for (uint32_t m = 0; m < f->partitions; ++m)
    {
        const float* w = f->background + (size_t)m * 2 * f->bins;
        double e = 0.0;
        for (uint32_t k = 0; k < f->bins; ++k)
        {
            e += (double)w[2 * k] * (double)w[2 * k] + (double)w[2 * k + 1] * (double)w[2 * k + 1];
        }
        f->shares[m] = sqrt(e);
        largest = fmax(largest, f->shares[m]);
    }
    for (uint32_t m = 0; m < f->partitions; ++m)
    {
        f->shares[m] = f->adapted ? f->shares[m] + 0.1 * largest + 1e-12 : 1.0;
        sum += f->shares[m];
    }
    for (uint32_t m = 0; m < f->partitions; ++m)
    {
        f->shares[m] = 0.99 * f->shares[m] / sum;
    }
}

// The background's step: each bin's rate, each partition's share of it,
// over the render's power smoothed over the filter plus a hundredth of
// its mean over the bins (so that bins the render leaves empty, as a
// band-limited stream played at a higher rate does, do not take steps
// from rounding noise).
static void Adapt(maudEchoFilter* f, double rer, double startRate)
{
    const float* newest = Partition(f->render, f->bins, f->head);
    double smooth = 0.35 / f->partitions;
    for (uint32_t k = 0; k < f->bins; ++k)
    {
        double p = (double)newest[2 * k] * (double)newest[2 * k] +
                   (double)newest[2 * k + 1] * (double)newest[2 * k + 1];
        f->slowPower[k] = (1.0 - smooth) * f->slowPower[k] + smooth * p + 1e-10;
    }
    double mean = 0.0;
    for (uint32_t k = 0; k < f->bins; ++k)
    {
        mean += f->slowPower[k];
    }
    double floorPower = POWER_FLOOR * mean / f->bins;
    for (uint32_t k = 0; k < f->bins; ++k)
    {
        float er = f->errorSpectrum[2 * k];
        float ei = f->errorSpectrum[2 * k + 1];
        const float* y = f->echoSpectrum + 2 * k;
        double e2 = (double)er * (double)er + (double)ei * (double)ei;
        double y2 = (double)y[0] * (double)y[0] + (double)y[1] * (double)y[1];
        double mu = f->adapted
                        ? LEAK_SHARE * fmin(f->leak * y2 / (e2 + 1e-12), MU_MAX) + RER_SHARE * rer
                        : startRate;
        for (uint32_t m = 0; m < f->partitions; ++m)
        {
            const float* x = Partition(f->render, f->bins, (f->head + m) % f->partitions);
            float* w = f->background + (size_t)m * 2 * f->bins + 2 * k;
            double step = mu * f->shares[m] / (f->slowPower[k] + floorPower);
            float xr = x[2 * k];
            float xi = x[2 * k + 1];
            w[0] += (float)(step * (double)(er * xr + ei * xi));
            w[1] += (float)(step * (double)(ei * xr - er * xi));
        }
    }
}

// One partition of the background in turn made a linear convolution:
// its impulse response's second half set to zero.
static void Constrain(maudEchoFilter* f)
{
    float* w = f->background + (size_t)f->constrain * 2 * f->bins;
    memcpy(f->spectrum, w, 2 * (size_t)f->bins * sizeof(float));
    maudInverseRealFft(f->fft, f->spectrum, f->time);
    memset(f->time + f->block, 0, f->block * sizeof(float));
    maudForwardRealFft(f->fft, f->time, w);
    f->constrain = (f->constrain + 1) % f->partitions;
}

// The rates' inputs: the residual-to-error ratio of the block (from the
// render's energy, so a filter that has learnt nothing still learns,
// and the leakage), and until the filter has adapted, the start rate.
static double Rates(maudEchoFilter* f, const float* render, const Sums* s, double* startRate)
{
    double sxx = 0.0;
    for (uint32_t i = 0; i < f->block; ++i)
    {
        sxx += (double)render[i] * (double)render[i];
    }
    double rer = fmin((1e-4 * sxx + 3.0 * f->leak * s->echo) / (s->background + 1e-12), MU_MAX);
    *startRate = 0.0;
    if (!f->adapted && sxx > f->block * 1e-6)
    {
        *startRate = fmin(START_RATE * sxx / (s->background + 1e-12), START_RATE);
        f->started += *startRate;
        f->adapted = f->started > f->partitions;
    }
    return rer;
}

void maudRunEchoFilter(maudEchoFilter* f, const float* render, const float* capture, float* error,
                       float* echo)
{
    uint32_t n = f->block;
    Push(f, render);
    // Both filters' echo estimates, in time: the second half of the
    // inverse transforms.
    float* yb = f->backgroundEcho;
    float* yf = f->foregroundEcho;
    float* eb = f->backgroundError;
    Estimate(f, f->background, f->spectrum);
    maudInverseRealFft(f->fft, f->spectrum, yb);
    Estimate(f, f->foreground, f->spectrum);
    maudInverseRealFft(f->fft, f->spectrum, yf);
    Sums s = {0};
    for (uint32_t i = 0; i < n; ++i)
    {
        eb[i] = capture[i] - yb[n + i];
        error[i] = capture[i] - yf[n + i];
        echo[i] = yf[n + i];
        s.background += (double)eb[i] * (double)eb[i];
        s.foreground += (double)error[i] * (double)error[i];
        s.echo += (double)yb[n + i] * (double)yb[n + i];
    }
    Paths(f, &s, yb + n, yf + n, eb, error, yb + n);
    Padded(f, eb, f->errorSpectrum);
    Padded(f, yb + n, f->echoSpectrum);
    Leakage(f, &s);
    double startRate = 0.0;
    double rer = Rates(f, render, &s, &startRate);
    Shares(f);
    Adapt(f, rer, startRate);
    Constrain(f);
}
