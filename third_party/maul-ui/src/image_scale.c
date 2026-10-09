// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Images scaled (record mui-0006). Shrinking, an output pixel is the
// average of the source over its area, each source pixel weighed by how
// much of it falls inside, the area outside the source clear. Growing,
// an output pixel is the source sampled bilinearly at its centre, pixels
// past the source's edge clear, so a factor of 1 at whole pixels copies
// the source exactly.

#include "image_scale.h"

#include <math.h>
#include <string.h>

// How much of source pixel i, spanning i to i + 1, lies between a and b.
static double Overlap(double i, double a, double b)
{
    double from = a > i ? a : i;
    double to = b < i + 1.0 ? b : i + 1.0;
    return to > from ? to - from : 0.0;
}

// The source's average over [u0, u1) x [v0, v1), in its pixels; the area
// past its edges counts as clear.
static void Average(const muiScaleSource* s, double u0, double u1, double v0, double v1, float* out)
{
    double sum[4] = {0.0, 0.0, 0.0, 0.0};
    int64_t xFirst = (int64_t)floor(u0) > 0 ? (int64_t)floor(u0) : 0;
    int64_t yFirst = (int64_t)floor(v0) > 0 ? (int64_t)floor(v0) : 0;
    int64_t xEnd = (int64_t)ceil(u1) < (int64_t)s->width ? (int64_t)ceil(u1) : (int64_t)s->width;
    int64_t yEnd = (int64_t)ceil(v1) < (int64_t)s->height ? (int64_t)ceil(v1) : (int64_t)s->height;
    for (int64_t y = yFirst; y < yEnd; y++)
    {
        double wy = Overlap((double)y, v0, v1);
        for (int64_t x = xFirst; x < xEnd; x++)
        {
            double w = wy * Overlap((double)x, u0, u1);
            const float* p = &s->pixels[((size_t)y * s->width + (size_t)x) * 4];
            for (int c = 0; c < 4; c++)
            {
                sum[c] += w * (double)p[c];
            }
        }
    }
    double area = (u1 - u0) * (v1 - v0);
    for (int c = 0; c < 4; c++)
    {
        out[c] = (float)(sum[c] / area);
    }
}

// A source pixel, clear past its edges.
static const float* Texel(const muiScaleSource* s, int64_t x, int64_t y)
{
    static const float CLEAR[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    return x < 0 || y < 0 || x >= (int64_t)s->width || y >= (int64_t)s->height
               ? CLEAR
               : &s->pixels[((size_t)y * s->width + (size_t)x) * 4];
}

// The source sampled bilinearly at u, v in its pixels, centres at halves.
static void Bilinear(const muiScaleSource* s, double u, double v, float* out)
{
    double x = u - 0.5;
    double y = v - 0.5;
    double x0 = floor(x);
    double y0 = floor(y);
    double fx = x - x0;
    double fy = y - y0;
    int64_t ix = (int64_t)x0;
    int64_t iy = (int64_t)y0;
    const float* a = Texel(s, ix, iy);
    const float* b = Texel(s, ix + 1, iy);
    const float* c = Texel(s, ix, iy + 1);
    const float* d = Texel(s, ix + 1, iy + 1);
    for (int k = 0; k < 4; k++)
    {
        double top = (double)a[k] + (double)(b[k] - a[k]) * fx;
        double bottom = (double)c[k] + (double)(d[k] - c[k]) * fx;
        out[k] = (float)(top + (bottom - top) * fy);
    }
}

void muiScaleImage(const muiScaleSource* source, double factor, double x, double y, float* out,
                   uint32_t width, uint32_t height)
{
    memset(out, 0, (size_t)width * height * 4 * sizeof(float));
    if (!(factor > 0.0) || source->width == 0 || source->height == 0)
    {
        return;
    }
    for (uint32_t row = 0; row < height; row++)
    {
        for (uint32_t column = 0; column < width; column++)
        {
            float* pixel = &out[((size_t)row * width + column) * 4];
            // The output pixel in the source's pixels.
            double u0 = ((double)column - x) / factor;
            double v0 = ((double)row - y) / factor;
            if (factor < 1.0)
            {
                Average(source, u0, u0 + 1.0 / factor, v0, v0 + 1.0 / factor, pixel);
            }
            else
            {
                Bilinear(source, u0 + 0.5 / factor, v0 + 0.5 / factor, pixel);
            }
        }
    }
}
