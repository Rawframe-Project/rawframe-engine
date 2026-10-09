// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 gradients (record mui-0006). A colour line's stops are
// sorted by offset, stably, so of stops at one offset the first colours
// below it and the last above it; outside the first and last offsets
// the line is padded, repeated or reflected. A linear gradient runs from
// p0 to p3, p1 projected onto the line through p0 perpendicular to p0p2;
// a radial one is the two-point conical gradient of CSS and canvas, each
// point given the largest t whose circle, of a radius not below 0, passes
// through it, and none where no circle does; a sweep's angles run
// counter-clockwise from the positive x axis, as the specification has
// it, stored with a bias of a half turn so that 360 degrees can be
// written (fontTools' BiasedAngle), which FreeType hands on as stored.
// Geometry without extent paints nothing.

#include "colr_gradient.h"

#include "motion_math.h"
#include "text_block.h"
#include "text_service.h"

#include <math.h>
#include <stdlib.h>

#if MUI_COLR_PAINT

// A stop: its offset, its place in the font, and its colour.
typedef struct Stop
{
    double offset;
    uint32_t order;
    muiLinearColor color;
} Stop;

typedef struct Line
{
    const Stop* stops;
    uint32_t count;
    FT_PaintExtend extend;
} Line;

static double Fixed(FT_Fixed value)
{
    return (double)value / 65536.0;
}

static int CompareStops(const void* a, const void* b)
{
    const Stop* left = a;
    const Stop* right = b;
    if (left->offset != right->offset)
    {
        return left->offset < right->offset ? -1 : 1;
    }
    return left->order < right->order ? -1 : (left->order > right->order ? 1 : 0);
}

// Reads a colour line's stops, sorted, into the service's buffer.
static muiResult ReadLine(const muiPaintSource* source, FT_ColorLine colorline, Line* lineOut)
{
    muiTextService* service = source->service;
    FT_ColorStopIterator iterator = colorline.color_stop_iterator;
    uint32_t count = iterator.num_color_stops;
    if (!muiReserve(&service->allocator, &service->paintStops, (size_t)count * sizeof(Stop)))
    {
        return mui_errorCapacity;
    }
    Stop* stops = service->paintStops.data;
    uint32_t read = 0;
    FT_ColorStop stop;
    while (read < count && FT_Get_Colorline_Stops(source->font->face, &stop, &iterator))
    {
        stops[read] = (Stop){Fixed(stop.stop_offset), read, muiColrPaintColor(source, stop.color)};
        read++;
    }
    if (read > 1)
    {
        qsort(stops, read, sizeof(Stop), CompareStops);
    }
    *lineOut = (Line){stops, read, colorline.extend};
    return mui_success;
}

// Where t falls on the line's interval once extended past its ends.
static double Extend(const Line* line, double t)
{
    double low = line->stops[0].offset;
    double span = line->stops[line->count - 1].offset - low;
    if (!(span > 0.0) || line->extend == FT_COLR_PAINT_EXTEND_PAD)
    {
        return t;
    }
    double u = (t - low) / span;
    if (line->extend == FT_COLR_PAINT_EXTEND_REPEAT)
    {
        return low + span * (u - floor(u));
    }
    // Reflected: every other interval runs backwards.
    double f = u - 2.0 * floor(u / 2.0);
    return low + span * (f > 1.0 ? 2.0 - f : f);
}

static muiLinearColor Mix(muiLinearColor a, muiLinearColor b, float k)
{
    return (muiLinearColor){a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
                            a.a + (b.a - a.a) * k};
}

// The line's colour at t.
static muiLinearColor ColorAt(const Line* line, double t)
{
    t = Extend(line, t);
    // The first stop past t.
    uint32_t low = 0;
    uint32_t high = line->count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        if (line->stops[middle].offset <= t)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low == 0)
    {
        return line->stops[0].color;
    }
    if (low == line->count)
    {
        return line->stops[line->count - 1].color;
    }
    const Stop* before = &line->stops[low - 1];
    const Stop* after = &line->stops[low];
    float k = (float)((t - before->offset) / (after->offset - before->offset));
    return Mix(before->color, after->color, k);
}

// A gradient's geometry in font units, and where a point falls on its
// colour line; false where it paints nothing.
typedef struct Geometry
{
    FT_PaintFormat format;
    // Linear: p0 and p3 - p0, and its length squared. Radial: c0, c1 - c0,
    // r0, r1 - r0, and |c1 - c0|^2 - (r1 - r0)^2. Sweep: the centre, the
    // start angle and the span to the end, in degrees.
    double x;
    double y;
    double dx;
    double dy;
    double r;
    double dr;
    double a;
} Geometry;

static bool LinearOf(const FT_PaintLinearGradient* g, Geometry* out)
{
    double x0 = Fixed(g->p0.x);
    double y0 = Fixed(g->p0.y);
    double d1x = Fixed(g->p1.x) - x0;
    double d1y = Fixed(g->p1.y) - y0;
    // The perpendicular of p0p2.
    double px = -(Fixed(g->p2.y) - y0);
    double py = Fixed(g->p2.x) - x0;
    double length = px * px + py * py;
    if (!(length > 0.0))
    {
        return false;
    }
    double k = (d1x * px + d1y * py) / length;
    *out = (Geometry){FT_COLR_PAINTFORMAT_LINEAR_GRADIENT, x0, y0, px * k, py * k, 0.0, 0.0, 0.0};
    out->a = out->dx * out->dx + out->dy * out->dy;
    return out->a > 0.0;
}

static bool RadialOf(const FT_PaintRadialGradient* g, Geometry* out)
{
    double x0 = Fixed(g->c0.x);
    double y0 = Fixed(g->c0.y);
    double dx = Fixed(g->c1.x) - x0;
    double dy = Fixed(g->c1.y) - y0;
    double r0 = Fixed(g->r0);
    double dr = Fixed(g->r1) - r0;
    *out = (Geometry){FT_COLR_PAINTFORMAT_RADIAL_GRADIENT, x0, y0, dx, dy, r0, dr,
                      dx * dx + dy * dy - dr * dr};
    // Two circles alike, or two points: no extent.
    return dx != 0.0 || dy != 0.0 || dr != 0.0;
}

static bool SweepOf(const FT_PaintSweepGradient* g, Geometry* out)
{
    double start = (Fixed(g->start_angle) + 1.0) * 180.0;
    double span = (Fixed(g->end_angle) + 1.0) * 180.0 - start;
    *out = (Geometry){FT_COLR_PAINTFORMAT_SWEEP_GRADIENT,
                      Fixed(g->center.x),
                      Fixed(g->center.y),
                      0.0,
                      0.0,
                      start,
                      span,
                      0.0};
    return span != 0.0;
}

// The largest t whose circle, its radius not below 0, passes through a
// point.
static bool RadialAt(const Geometry* g, double x, double y, double* tOut)
{
    double px = x - g->x;
    double py = y - g->y;
    double b = px * g->dx + py * g->dy + g->r * g->dr;
    double c = px * px + py * py - g->r * g->r;
    if (g->a == 0.0)
    {
        *tOut = c / (2.0 * b);
        return b != 0.0 && g->r + *tOut * g->dr >= 0.0;
    }
    double discriminant = b * b - g->a * c;
    if (discriminant < 0.0)
    {
        return false;
    }
    double root = sqrt(discriminant);
    double one = (b + root) / g->a;
    double other = (b - root) / g->a;
    double larger = one > other ? one : other;
    double smaller = one > other ? other : one;
    *tOut = g->r + larger * g->dr >= 0.0 ? larger : smaller;
    return g->r + *tOut * g->dr >= 0.0;
}

static bool At(const Geometry* g, double x, double y, double* tOut)
{
    switch (g->format)
    {
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
        *tOut = ((x - g->x) * g->dx + (y - g->y) * g->dy) / g->a;
        return true;
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
        return RadialAt(g, x, y, tOut);
    default:
    {
        const double degrees = 180.0 / 3.14159265358979323846;
        double angle = muiAtan2(y - g->y, x - g->x) * degrees;
        angle = angle < 0.0 ? angle + 360.0 : angle;
        *tOut = (angle - g->r) / g->dr;
        return true;
    }
    }
}

static bool GeometryOf(const FT_COLR_Paint* paint, Geometry* out)
{
    switch (paint->format)
    {
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
        return LinearOf(&paint->u.linear_gradient, out);
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
        return RadialOf(&paint->u.radial_gradient, out);
    default:
        return SweepOf(&paint->u.sweep_gradient, out);
    }
}

static FT_ColorLine LineOf(const FT_COLR_Paint* paint)
{
    switch (paint->format)
    {
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
        return paint->u.linear_gradient.colorline;
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
        return paint->u.radial_gradient.colorline;
    default:
        return paint->u.sweep_gradient.colorline;
    }
}

// Pixels' centres taken back into font units: the matrix inverted, with
// the size's scale and the pen's offset undone first.
typedef struct Inverse
{
    double xx;
    double xy;
    double yx;
    double yy;
    double dx;
    double dy;
    double scale;
    double offset;
} Inverse;

static bool InverseOf(const muiPaintSource* source, muiPaintMatrix m, Inverse* out)
{
    double det = m.xx * m.yy - m.xy * m.yx;
    if (!(fabs(det) > 0.0) || !isfinite(det))
    {
        return false;
    }
    *out = (Inverse){m.yy / det,
                     -m.xy / det,
                     -m.yx / det,
                     m.xx / det,
                     m.dx,
                     m.dy,
                     (double)source->size / (double)source->font->face->units_per_EM,
                     (double)source->offset};
    return true;
}

muiResult muiPaintGradient(const muiPaintSource* source, const FT_COLR_Paint* paint,
                           muiPaintMatrix m, const muiPixelBox* box, float* pixels)
{
    Geometry geometry;
    Inverse inverse;
    if (!GeometryOf(paint, &geometry) || !InverseOf(source, m, &inverse))
    {
        return mui_success;
    }
    Line line;
    muiResult result = ReadLine(source, LineOf(paint), &line);
    if (result != mui_success || line.count == 0)
    {
        return result;
    }
    for (FT_Pos row = 0; row < box->height; row++)
    {
        // Pixel centres in 64ths, then in the matrix's font units.
        double v =
            (double)((box->bottom + box->height - 1 - row) * 64 + 32) / inverse.scale - inverse.dy;
        for (FT_Pos column = 0; column < box->width; column++)
        {
            double u = ((double)((box->left + column) * 64 + 32) - inverse.offset) / inverse.scale -
                       inverse.dx;
            double t = 0.0;
            if (!At(&geometry, inverse.xx * u + inverse.xy * v, inverse.yx * u + inverse.yy * v,
                    &t) ||
                !isfinite(t))
            {
                continue;
            }
            muiLinearColor color = ColorAt(&line, t);
            float* pixel = &pixels[((size_t)row * (size_t)box->width + (size_t)column) * 4];
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
            pixel[3] = color.a;
        }
    }
    return mui_success;
}

#endif
