// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Paths. A Catmull-Rom segment is the
// cubic Hermite whose end tangents Yuksel, Schaefer and Keyser give for
// knots spaced by the chord's length to the power alpha:
//   m1 = d12 ((P1 - P0) / d01 - (P2 - P0) / (d01 + d12) + (P2 - P1) / d12)
//   m2 = d12 ((P2 - P1) / d12 - (P3 - P1) / (d12 + d23) + (P3 - P2) / d23)
// with the ends' missing neighbors reflected, and each gap raised to
// 1e-12 of the largest of the three, so the curve is the same at any
// scale and coincident keyframes hold still. Arc length is adaptive
// Gauss-Kronrod, 15 points against 7 agreeing to 1e-12 relative, split at
// the speed's minima, and the parameter at a length is safeguarded Halley
// on it.

#include "maul-tween/path.h"

#include "bounds_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "invariant.h"
#include "path_internal.h"
#include "path_length.h"
#include "valid.h"
#include "work.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

// The smallest knot gap, as a share of the segment's largest.
static const double s_smallestGap = 1e-12;
static bool IsFinite4(mtwnVec4 v, uint8_t channels)
{
    const float* c = &v.x;
    for (uint8_t i = 0; i < channels; ++i)
    {
        if (!isfinite(c[i]))
        {
            return false;
        }
    }
    return true;
}

static void Load(mtwnVec4 v, double out[4])
{
    out[0] = (double)v.x;
    out[1] = (double)v.y;
    out[2] = (double)v.z;
    out[3] = (double)v.w;
}

// A keyframe's value or control as the path moves it: a scale's
// logarithm on a logarithmic path.
static void LoadOf(const mtwnPath* path, mtwnVec4 v, double out[4])
{
    Load(v, out);
    if (path->logarithmic)
    {
        out[0] = mtwnLog(out[0]);
    }
}

// The knot gap between two points: their distance to the power alpha.
static double Gap(const double p[4], const double q[4], uint8_t channels, double alpha)
{
    double sum = 0.0;
    for (uint8_t i = 0; i < channels; ++i)
    {
        double d = q[i] - p[i];
        sum += d * d;
    }
    double chord = mtwnRootOfSquares(sum);
    double gap = 1.0;
    if (alpha == 0.5)
    {
        gap = sqrt(chord);
    }
    else if (alpha != 0.0)
    {
        gap = mtwnPow(chord, alpha);
    }
    return gap;
}

// A Hermite segment from p1 to p2 with end tangents m1 and m2.
static void Hermite(const double p1[4], const double p2[4], const double m1[4], const double m2[4],
                    mtwnCubic* out)
{
    for (int i = 0; i < 4; ++i)
    {
        out->a[i] = p1[i];
        out->b[i] = m1[i];
        out->c[i] = 3.0 * (p2[i] - p1[i]) - 2.0 * m1[i] - m2[i];
        out->d[i] = 2.0 * (p1[i] - p2[i]) + m1[i] + m2[i];
    }
}

// Through p1 and p2, keyframes i and i + 1 as the path moves them.
static void CatmullRom(const mtwnPath* path, uint32_t i, const double p1[4], const double p2[4],
                       mtwnCubic* out)
{
    const mtwnKeyframe* k = path->keyframes;
    double p0[4];
    double p3[4];
    for (int c = 0; c < 4; ++c)
    {
        p0[c] = 2.0 * p1[c] - p2[c];
        p3[c] = 2.0 * p2[c] - p1[c];
    }
    if (i > 0)
    {
        LoadOf(path, k[i - 1].value, p0);
    }
    if (i + 2 < path->count)
    {
        LoadOf(path, k[i + 2].value, p3);
    }
    uint8_t n = path->channels;
    double d01 = Gap(p0, p1, n, path->alpha);
    double d12 = Gap(p1, p2, n, path->alpha);
    double d23 = Gap(p2, p3, n, path->alpha);
    // Gaps relative to the largest; all four points coincident, any gap
    // gives the same still segment.
    double largest = d01 > d12 ? d01 : d12;
    largest = d23 > largest ? d23 : largest;
    double least = largest > 0.0 ? s_smallestGap * largest : 1.0;
    d01 = d01 > least ? d01 : least;
    d12 = d12 > least ? d12 : least;
    d23 = d23 > least ? d23 : least;
    // Every gap divides: raised to least, which is positive, even where a
    // host's changed keyframes made a gap NaN.
    MTWN_ASSERT(d01 > 0.0 && d12 > 0.0 && d23 > 0.0);
    double m1[4];
    double m2[4];
    for (int c = 0; c < 4; ++c)
    {
        m1[c] =
            d12 * ((p1[c] - p0[c]) / d01 - (p2[c] - p0[c]) / (d01 + d12) + (p2[c] - p1[c]) / d12);
        m2[c] =
            d12 * ((p2[c] - p1[c]) / d12 - (p3[c] - p1[c]) / (d12 + d23) + (p3[c] - p2[c]) / d23);
    }
    Hermite(p1, p2, m1, m2, out);
}

// The sign of a secant, 0 counted as rising, as Steffen's slope takes it.
static double SignOf(double s)
{
    return s < 0.0 ? -1.0 : 1.0;
}

// Steffen's slope at a keyframe between secants a, over a width of ha,
// and b, over hb: 0 where they differ in sign, else at most twice the
// lesser and the parabola's through the three keyframes.
static double Steffen(double a, double b, double ha, double hb)
{
    double p = (a * hb + b * ha) / (ha + hb);
    double least = fabs(a) < fabs(b) ? fabs(a) : fabs(b);
    least = 0.5 * fabs(p) < least ? 0.5 * fabs(p) : least;
    double slope = (SignOf(a) + SignOf(b)) * least;
    // Steffen's bound, which keeps the segment monotone: at most twice the
    // lesser secant, and not against either's sign. NaN secants pass.
    MTWN_ASSERT(!(fabs(slope) > 2.0 * fabs(a)) && !(fabs(slope) > 2.0 * fabs(b)) &&
                !(slope * a < 0.0) && !(slope * b < 0.0));
    return slope;
}

// Whether a keyframe's neighbor on one side leaves a width to measure a
// secant over: not past the ends, and not a jump.
static bool Measured(double width)
{
    return width > 0x1p-52;
}

// Channel c of a monotone segment, as Hermite makes it: points hold
// keyframes i - 1 to i + 2 as the path moves them, widths the offsets'
// between them,
// has whether the width before and after is measured; a jump, not
// measured itself, is its line. Out of line, the four channels one call
// each: unrolled they took 1.8 kB.
static MTWN_NOINLINE void MonotoneChannel(const double* const points[4], const double widths[3],
                                          const bool has[2], int c, mtwnCubic* out)
{
    double p[4] = {points[0][c], points[1][c], points[2][c], points[3][c]};
    double m1 = p[2] - p[1];
    double m2 = m1;
    if (Measured(widths[1]))
    {
        double s = m1 / widths[1];
        double t1 = has[0] ? Steffen((p[1] - p[0]) / widths[0], s, widths[0], widths[1]) : s;
        double t2 = has[1] ? Steffen(s, (p[3] - p[2]) / widths[2], widths[1], widths[2]) : s;
        if (has[0] != has[1])
        {
            t1 = has[0] ? t1 : 0.5 * (3.0 * s - t2);
            t2 = has[1] ? t2 : 0.5 * (3.0 * s - t1);
        }
        m1 = t1 * widths[1];
        m2 = t2 * widths[1];
    }
    out->a[c] = p[1];
    out->b[c] = m1;
    out->c[c] = 3.0 * (p[2] - p[1]) - 2.0 * m1 - m2;
    out->d[c] = 2.0 * (p[1] - p[2]) + m1 + m2;
}

// A monotone segment: channel by channel, the Hermite
// cubic of Steffen's slopes in time, the offsets the time; at an end, or
// beside a jump, d3's one-sided slope (3 s - t) / 2, of curvature 0;
// with neither side measured, the line. p1 and p2 are keyframes i and
// i + 1 as the path moves them.
static void Monotone(const mtwnPath* path, uint32_t i, const double p1[4], const double p2[4],
                     mtwnCubic* out)
{
    const mtwnKeyframe* k = path->keyframes;
    double widths[3] = {i > 0 ? k[i].offset - k[i - 1].offset : 0.0, k[i + 1].offset - k[i].offset,
                        i + 2 < path->count ? k[i + 2].offset - k[i + 1].offset : 0.0};
    bool measured = Measured(widths[1]);
    bool has[2] = {measured && Measured(widths[0]), measured && Measured(widths[2])};
    // A width past the ends is 0, never measured, so a neighbor read is
    // a keyframe of the path.
    MTWN_ASSERT((!has[0] || i > 0) && (!has[1] || i + 2 < path->count));
    double p0[4] = {0.0, 0.0, 0.0, 0.0};
    double p3[4] = {0.0, 0.0, 0.0, 0.0};
    if (has[0])
    {
        LoadOf(path, k[i - 1].value, p0);
    }
    if (has[1])
    {
        LoadOf(path, k[i + 2].value, p3);
    }
    const double* const points[4] = {p0, p1, p2, p3};
    // The path's channels only: SegmentCubic left the others 0.
    for (int c = 0; c < path->channels; ++c)
    {
        MonotoneChannel(points, widths, has, c, out);
    }
}

// Segment i, from keyframe i to keyframe i + 1, as a cubic.
static void SegmentCubic(const mtwnPath* path, uint32_t i, mtwnCubic* out)
{
    MTWN_ASSERT(i + 1 < path->count);
    const mtwnKeyframe* k = path->keyframes;
    memset(out, 0, sizeof *out);
    double p0[4];
    double p3[4];
    LoadOf(path, k[i].value, p0);
    LoadOf(path, k[i + 1].value, p3);
    switch (k[i].kind)
    {
    case mtwn_segmentStep:
        memcpy(out->a, p0, sizeof p0);
        out->step = true;
        return;
    case mtwn_segmentBezier:
    {
        double p1[4];
        double p2[4];
        LoadOf(path, k[i].out, p1);
        LoadOf(path, k[i + 1].in, p2);
        for (int c = 0; c < 4; ++c)
        {
            out->a[c] = p0[c];
            out->b[c] = 3.0 * (p1[c] - p0[c]);
            out->c[c] = 3.0 * (p0[c] - 2.0 * p1[c] + p2[c]);
            out->d[c] = p3[c] - p0[c] + 3.0 * (p1[c] - p2[c]);
        }
        return;
    }
    case mtwn_segmentCatmullRom:
        CatmullRom(path, i, p0, p3, out);
        return;
    case mtwn_segmentMonotone:
        Monotone(path, i, p0, p3, out);
        return;
    default:
        for (int c = 0; c < 4; ++c)
        {
            out->a[c] = p0[c];
            out->b[c] = p3[c] - p0[c];
        }
        return;
    }
}

// The cubic's value at u in its first channels, exact at its ends: at 0
// its start, at 1 the end keyframe's value; a step holds its start until
// u reaches 1. The tangent too, unless it is NULL.
static void Evaluate(const mtwnCubic* cubic, const double end[4], double u, uint8_t channels,
                     double value[4], double* tangent)
{
    for (uint8_t c = 0; c < channels; ++c)
    {
        if (cubic->step)
        {
            value[c] = u >= 1.0 ? end[c] : cubic->a[c];
            continue;
        }
        value[c] = u == 1.0 ? end[c]
                            : cubic->a[c] + u * (cubic->b[c] + u * (cubic->c[c] + u * cubic->d[c]));
    }
    if (tangent == NULL)
    {
        return;
    }
    for (uint8_t c = 0; c < channels; ++c)
    {
        tangent[c] =
            cubic->step ? 0.0 : cubic->b[c] + u * (2.0 * cubic->c[c] + u * 3.0 * cubic->d[c]);
    }
}

static bool IsValid(const mtwnKeyframe* keyframes, uint32_t count, uint8_t channels, double alpha,
                    bool constantSpeed)
{
    if (keyframes == NULL || count < 2 || channels < 1 || channels > 4 || !isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0 || keyframes[0].offset != 0.0 ||
        keyframes[count - 1].offset != 1.0)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const mtwnKeyframe* k = &keyframes[i];
        bool segment = i + 1 < count;
        if (!isfinite(k->offset) || (segment && keyframes[i + 1].offset < k->offset) ||
            k->kind > mtwn_segmentMonotone || !IsFinite4(k->value, channels) ||
            !IsFinite4(k->in, channels) || !IsFinite4(k->out, channels) ||
            (constantSpeed && segment && k->kind == mtwn_segmentStep) ||
            !mtwnValidEasing(&k->easing))
        {
            return false;
        }
    }
    return true;
}

// A path, on a scale track's over its keyframes' logarithms.
static mtwnResult MakePathOf(const mtwnKeyframe* keyframes, uint32_t count, uint8_t channels,
                             double alpha, bool logarithmic, double* lengthsOut, mtwnPath* pathOut)
{
    if (pathOut == NULL || !IsValid(keyframes, count, channels, alpha, lengthsOut != NULL))
    {
        return mtwn_errorInvalid;
    }
    mtwnPath path = {keyframes, lengthsOut, alpha, count, channels, logarithmic};
    if (lengthsOut != NULL)
    {
        lengthsOut[0] = 0.0;
        for (uint32_t i = 0; i + 1 < count; ++i)
        {
            mtwnCubic cubic;
            SegmentCubic(&path, i, &cubic);
            mtwnMinima minima = mtwnSpeedMinima(&cubic, channels);
            lengthsOut[i + 1] = lengthsOut[i] + mtwnArcLength(&cubic, channels, &minima, 0.0, 1.0);
        }
    }
    *pathOut = path;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnMakePathWork(const mtwnKeyframe* keyframes, uint32_t count,
                                                 uint8_t channels, double alpha, double* lengthsOut,
                                                 mtwnPath* pathOut)
{
    return MakePathOf(keyframes, count, channels, alpha, false, lengthsOut, pathOut);
}

mtwnResult mtwnMakePath(const mtwnKeyframe* keyframes, uint32_t count, uint8_t channels,
                        double alpha, double* lengthsOut, mtwnPath* pathOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakePathWork(keyframes, count, channels, alpha, lengthsOut, pathOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// The last segment whose start is at or below key in values, from 0 to
// count - 2.
static uint32_t FindSegment(const mtwnPath* path, double key, bool byLength)
{
    uint32_t lo = 0;
    uint32_t hi = path->count - 2;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo + 1) / 2;
        double start = byLength ? path->lengths[mid] : path->keyframes[mid].offset;
        if (start <= key)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    // A segment of the path whatever the key and the values, NaN or out of
    // order as a host's changed keyframes may leave them.
    MTWN_ASSERT(lo + 1 < path->count);
    return lo;
}

// Segment i's parameter at a progress: its local progress eased.
static double Eased(const mtwnPath* path, uint32_t i, double progress)
{
    const mtwnKeyframe* k = path->keyframes;
    // A width within progress's rounding of none jumps, as equal offsets
    // do: extended by it, a width of 1e-300 took progress -1.5 to a local
    // progress of -1.5e300.
    double width = k[i + 1].offset - k[i].offset;
    double local = width > 0x1p-52 ? (progress - k[i].offset) / width : 1.0;
    return mtwnSampleEasingWork(&k[i].easing, local, false);
}

// The segment a progress falls in and the parameter within it.
// The segment a progress falls in: by its share of the length at constant
// speed, else by the offsets.
static uint32_t SegmentAt(const mtwnPath* path, double progress)
{
    if (path->lengths != NULL)
    {
        return FindSegment(path, progress * path->lengths[path->count - 1], true);
    }
    return FindSegment(path, progress, false);
}

// The parameter within segment i, the one a progress falls in, and its
// cubic.
static double LocateIn(const mtwnPath* path, uint32_t i, double progress, mtwnCubic* cubic)
{
    SegmentCubic(path, i, cubic);
    if (path->lengths == NULL)
    {
        return Eased(path, i, progress);
    }
    double total = path->lengths[path->count - 1];
    double distance = progress * total;
    double length = path->lengths[i + 1] - path->lengths[i];
    // A segment within the total's rounding of none holds still, as one of
    // none: extended by its parameter, a length of 1e-23 took a millionth
    // of the path before it to 1e33.
    return length > total * 0x1p-52
               ? mtwnParameterAt(cubic, path->channels, distance - path->lengths[i], length)
               : 1.0;
}

// The segment a progress falls in and the parameter within it.
static void Locate(const mtwnPath* path, double progress, uint32_t* segmentOut, double* uOut,
                   mtwnCubic* cubic)
{
    uint32_t i = SegmentAt(path, progress);
    *uOut = LocateIn(path, i, progress, cubic);
    *segmentOut = i;
}

// A linear segment's value and tangent, as Evaluate gives them for its
// cubic (a = p0, b = p1 - p0), the same bits without making it.
static void SampleLinear(const mtwnPath* path, uint32_t i, double u, double valueOut[4],
                         double tangentOut[4])
{
    double p0[4];
    double p1[4];
    LoadOf(path, path->keyframes[i].value, p0);
    LoadOf(path, path->keyframes[i + 1].value, p1);
    double tangent[4];
    for (int c = 0; c < 4; ++c)
    {
        tangent[c] = c < path->channels ? p1[c] - p0[c] : 0.0;
        valueOut[c] = c >= path->channels ? 0.0 : (u == 1.0 ? p1[c] : p0[c] + u * tangent[c]);
    }
    if (tangentOut != NULL)
    {
        memcpy(tangentOut, tangent, sizeof tangent);
    }
}

MTWN_NOINLINE bool mtwnValidPath(const mtwnPath* path)
{
    return path != NULL && path->keyframes != NULL && path->count >= 2 && path->channels >= 1 &&
           path->channels <= 4 && path->alpha >= 0.0 && path->alpha <= 1.0 &&
           path->logarithmic <= 1 && (path->logarithmic == 0 || path->channels == 1) &&
           (path->lengths == NULL ||
            (path->lengths[0] == 0.0 && isfinite(path->lengths[path->count - 1])));
}

// Whether a sample may read segment i: its kind known and, timed by its
// keyframes, its easing a maker's; at constant speed not a step, which has
// no length to travel.
static bool Reads(const mtwnPath* path, uint32_t i)
{
    const mtwnKeyframe* k = &path->keyframes[i];
    if (path->lengths != NULL)
    {
        return k->kind <= mtwn_segmentMonotone && k->kind != mtwn_segmentStep;
    }
    return k->kind <= mtwn_segmentMonotone && mtwnValidEasing(&k->easing);
}

bool mtwnPathReadsValid(const mtwnPath* path, double progress)
{
    return isfinite(progress) && Reads(path, SegmentAt(path, progress));
}

// A path's sample in segment i, the one a progress falls in.
static void SampleIn(const mtwnPath* path, uint32_t i, double progress, double valueOut[4],
                     double tangentOut[4])
{
    // Timed by its keyframes, a linear segment needs no cubic.
    if (path->lengths == NULL && path->keyframes[i].kind == mtwn_segmentLinear)
    {
        SampleLinear(path, i, Eased(path, i, progress), valueOut, tangentOut);
        return;
    }
    mtwnCubic cubic;
    double u = LocateIn(path, i, progress, &cubic);
    double end[4];
    LoadOf(path, path->keyframes[i + 1].value, end);
    Evaluate(&cubic, end, u, path->channels, valueOut, tangentOut);
    for (uint8_t c = path->channels; c < 4; ++c)
    {
        valueOut[c] = 0.0;
        if (tangentOut != NULL)
        {
            tangentOut[c] = 0.0;
        }
    }
}

static MTWN_NOINLINE mtwnResult mtwnSamplePathChecked(const mtwnPath* path, double progress,
                                                      double valueOut[4], double tangentOut[4])
{
    if (valueOut == NULL || !mtwnValidPath(path) || !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    uint32_t i = SegmentAt(path, progress);
    if (!Reads(path, i))
    {
        return mtwn_errorInvalid;
    }
    double value[4];
    double tangent[4];
    SampleIn(path, i, progress, value, tangentOut != NULL ? tangent : NULL);
    memcpy(valueOut, value, sizeof value);
    if (tangentOut != NULL)
    {
        memcpy(tangentOut, tangent, sizeof tangent);
    }
    return mtwn_success;
}

mtwnResult mtwnSamplePath(const mtwnPath* path, double progress, double valueOut[4],
                          double tangentOut[4])
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSamplePathChecked(path, progress, valueOut, tangentOut);
    mtwnFloatEnvLeave(env);
    return result;
}

void mtwnLocatePath(const mtwnPath* path, double progress, uint32_t* segmentOut, double* uOut,
                    mtwnCubic* cubic)
{
    // Its callers checked the path (src/path_direction.c).
    MTWN_ASSERT(mtwnValidPath(path));
    Locate(path, progress, segmentOut, uOut, cubic);
}

void mtwnPathSegmentCubic(const mtwnPath* path, uint32_t i, mtwnCubic* cubic)
{
    SegmentCubic(path, i, cubic);
}

static const uint8_t s_kindChannels[] = {1, 2, 3, 4, 4, 1, 4, 1, 1, 1};

// Whether a kind's track is its path's numbers: a scale's are their
// logarithms, which constant speed takes as well.
static bool IsNumberKind(mtwnValueKind kind)
{
    return kind != mtwn_valueColor && kind != mtwn_valueRotation && kind != mtwn_valueDiscrete &&
           kind != mtwn_valueScale;
}

// Whether the track's kind takes every segment its keyframes start.
static bool TakesSegments(mtwnValueKind kind, const mtwnKeyframe* keyframes, uint32_t count)
{
    if (kind != mtwn_valueColor && kind != mtwn_valueDiscrete)
    {
        return true;
    }
    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        if (keyframes[i].kind != mtwn_segmentLinear && keyframes[i].kind != mtwn_segmentStep)
        {
            return false;
        }
    }
    return true;
}

static bool IsZero4(mtwnVec4 v)
{
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f && v.w == 0.0f;
}

static bool CheckTrack(const mtwnTrackDef* def, const mtwnKeyframe* keyframes, uint32_t count,
                       bool lengths, const mtwnSegmentMix* mixes)
{
    if (def == NULL || def->kind > mtwn_valueScale || def->rotationPath > mtwn_rotationLongest ||
        (def->kind == mtwn_valueAngle && !mtwnIsAngleMethod(def->angleMethod)) ||
        !IsValid(keyframes, count, s_kindChannels[def->kind], def->alpha, lengths) ||
        (lengths && !IsNumberKind(def->kind) && def->kind != mtwn_valueScale) ||
        !TakesSegments(def->kind, keyframes, count))
    {
        return false;
    }
    // A scale's values, and a Bezier segment's controls, above 0.
    for (uint32_t i = 0; def->kind == mtwn_valueScale && i < count; ++i)
    {
        bool bezier = i + 1 < count && keyframes[i].kind == mtwn_segmentBezier;
        bool ending = i > 0 && keyframes[i - 1].kind == mtwn_segmentBezier;
        if (!(keyframes[i].value.x > 0.0f) || (bezier && !(keyframes[i].out.x > 0.0f)) ||
            (ending && !(keyframes[i].in.x > 0.0f)))
        {
            return false;
        }
    }
    bool mixed = def->kind == mtwn_valueColor || def->kind == mtwn_valueRotation;
    if (mixed && mixes == NULL)
    {
        return false;
    }
    for (uint32_t i = 0; def->kind == mtwn_valueRotation && i < count; ++i)
    {
        if (IsZero4(keyframes[i].value))
        {
            return false;
        }
    }
    return true;
}

// A control moved by its keyframe's whole turns, in double: the shift can
// pass the floats' range (from -3e38 to 3e38 is 6e38), and a control far
// from its keyframe can land past it, held as every number is.
static void Shift(mtwnVec4* v, double by)
{
    v->x = (float)mtwnNumberHeld((double)v->x + by);
}

static void Negate(mtwnVec4* v)
{
    *v = (mtwnVec4){-v->x, -v->y, -v->z, -v->w};
}

static double Dot4(mtwnVec4 a, mtwnVec4 b)
{
    return (double)a.x * (double)b.x + (double)a.y * (double)b.y + (double)a.z * (double)b.z +
           (double)a.w * (double)b.w;
}

// Angles each moved by the method's whole turns from the one before;
// rotations on the shortest path each into the hemisphere of the one
// before; their controls with them.
static void Canonicalize(const mtwnTrackDef* def, mtwnKeyframe* keyframes, uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i)
    {
        mtwnKeyframe* k = &keyframes[i];
        if (def->kind == mtwn_valueAngle)
        {
            float moved = (float)mtwnMixAngleWork((double)keyframes[i - 1].value.x,
                                                  (double)k->value.x, def->angleMethod, 1.0);
            double by = (double)moved - (double)k->value.x;
            k->value.x = moved;
            Shift(&k->in, by);
            Shift(&k->out, by);
        }
        else if (def->kind == mtwn_valueRotation && def->rotationPath == mtwn_rotationShortest &&
                 Dot4(keyframes[i - 1].value, k->value) < 0.0)
        {
            Negate(&k->value);
            Negate(&k->in);
            Negate(&k->out);
        }
    }
}

MTWN_NOINLINE mtwnResult mtwnMakeTrackWork(const mtwnTrackDef* def, mtwnKeyframe* keyframes,
                                           uint32_t count, double* lengthsOut,
                                           mtwnSegmentMix* mixesOut, mtwnTrack* trackOut)
{
    if (trackOut == NULL || !CheckTrack(def, keyframes, count, lengthsOut != NULL, mixesOut))
    {
        return mtwn_errorInvalid;
    }
    // Only the longest way between equal rotations refuses a mix: each is
    // tried before anything is written, so a refusal leaves the keyframes
    // and the mixes as they were.
    for (uint32_t i = 0; def->kind == mtwn_valueRotation &&
                         def->rotationPath == mtwn_rotationLongest && i + 1 < count;
         ++i)
    {
        mtwnVec4 a = keyframes[i].value;
        mtwnVec4 b = keyframes[i + 1].value;
        mtwnRotationMix tried;
        if (keyframes[i].kind == mtwn_segmentLinear &&
            mtwnMakeRotationMixWork((mtwnQuat){a.x, a.y, a.z, a.w}, (mtwnQuat){b.x, b.y, b.z, b.w},
                                    def->rotationPath, &tried) != mtwn_success)
        {
            return mtwn_errorInvalid;
        }
    }
    // What the canonical form writes is finite (an angle within a turn of
    // a float before it, a control held), so the path's check after it
    // refuses nothing the check before it passed.
    Canonicalize(def, keyframes, count);
    MTWN_ASSERT(
        IsValid(keyframes, count, s_kindChannels[def->kind], def->alpha, lengthsOut != NULL));
    bool mixed = def->kind == mtwn_valueColor || def->kind == mtwn_valueRotation;
    for (uint32_t i = 0; mixed && i + 1 < count; ++i)
    {
        mtwnVec4 a = keyframes[i].value;
        mtwnVec4 b = keyframes[i + 1].value;
        mtwnResult made = mtwn_success;
        if (def->kind == mtwn_valueColor)
        {
            made = mtwnMakeColorMixWork((mtwnColor){a.x, a.y, a.z, a.w},
                                        (mtwnColor){b.x, b.y, b.z, b.w}, def->colorSpace,
                                        def->hueMethod, &mixesOut[i].color);
        }
        else if (keyframes[i].kind == mtwn_segmentLinear)
        {
            made = mtwnMakeRotationMixWork((mtwnQuat){a.x, a.y, a.z, a.w},
                                           (mtwnQuat){b.x, b.y, b.z, b.w}, def->rotationPath,
                                           &mixesOut[i].rotation);
        }
        if (made != mtwn_success)
        {
            return mtwn_errorInvalid;
        }
    }
    mtwnTrack track = {{0}, mixed ? mixesOut : NULL, def->kind};
    mtwnResult result = MakePathOf(keyframes, count, s_kindChannels[def->kind], def->alpha,
                                   def->kind == mtwn_valueScale, lengthsOut, &track.path);
    if (result != mtwn_success)
    {
        return result;
    }
    *trackOut = track;
    return mtwn_success;
}

mtwnResult mtwnMakeTrack(const mtwnTrackDef* def, mtwnKeyframe* keyframes, uint32_t count,
                         double* lengthsOut, mtwnSegmentMix* mixesOut, mtwnTrack* trackOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeTrackWork(def, keyframes, count, lengthsOut, mixesOut, trackOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// A number track's sample as floats, held within the finite floats:
// converted together, then held together.
static mtwnVec4 ToVec4(const double v[4])
{
    return mtwnNumbersHeld((mtwnVec4){(float)v[0], (float)v[1], (float)v[2], (float)v[3]});
}

// A cubic extrapolated far enough to overflow its squares, scaled by its
// largest component first; components past the double range dominate the
// direction, each as 1 or -1 and the others as 0.
static double ScaledNorm(const double v[4], double scaled[4])
{
    double largest = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        largest = fabs(v[i]) > largest ? fabs(v[i]) : largest;
    }
    double sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        scaled[i] = isinf(largest) ? (isinf(v[i]) ? copysign(1.0, v[i]) : 0.0) : v[i] / largest;
        sum += scaled[i] * scaled[i];
    }
    return sqrt(sum);
}

// A rotation's per-component cubic, normalized; the segment's start where
// it cancels to zero.
static mtwnVec4 Normalized(const double v[4], mtwnVec4 start)
{
    double scaled[4] = {v[0], v[1], v[2], v[3]};
    double norm = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
    if (isinf(norm))
    {
        norm = ScaledNorm(v, scaled);
    }
    if (!(norm > 0.0))
    {
        return start;
    }
    return (mtwnVec4){(float)(scaled[0] / norm), (float)(scaled[1] / norm),
                      (float)(scaled[2] / norm), (float)(scaled[3] / norm)};
}

// A scale track's value: its keyframes exactly, a linear segment as the
// scale's mix, the others the exponential of their cubic in the
// logarithm.
static mtwnVec4 SampleScale(const mtwnPath* path, uint32_t i, double u, const mtwnCubic* cubic)
{
    const mtwnKeyframe* k = path->keyframes;
    if (k[i].kind == mtwn_segmentStep || u == 0.0 || u == 1.0)
    {
        return u >= 1.0 ? k[i + 1].value : k[i].value;
    }
    double from = (double)k[i].value.x;
    double scale = 0.0;
    if (k[i].kind == mtwn_segmentLinear)
    {
        mtwnScaleMix mix = {from, (double)k[i + 1].value.x, mtwnLog(from),
                            mtwnLog((double)k[i + 1].value.x) - mtwnLog(from)};
        scale = mtwnMixScaleWork(&mix, u);
    }
    else
    {
        double end[4];
        double value[4];
        LoadOf(path, k[i + 1].value, end);
        Evaluate(cubic, end, u, 1, value, NULL);
        scale = mtwnExp(value[0]);
    }
    return (mtwnVec4){mtwnScaleFloat(scale), 0.0f, 0.0f, 0.0f};
}

// A track's sample in segment i, the one a progress falls in.
static mtwnVec4 TrackIn(const mtwnTrack* track, uint32_t i, double progress)
{
    const mtwnPath* path = &track->path;
    double value[4];
    if (IsNumberKind(track->kind))
    {
        SampleIn(path, i, progress, value, NULL);
        if (track->kind == mtwn_valueInteger)
        {
            value[0] = round(value[0]);
        }
        return ToVec4(value);
    }
    mtwnCubic cubic;
    double u = LocateIn(path, i, progress, &cubic);
    const mtwnKeyframe* k = path->keyframes;
    mtwnSegmentKind kind = k[i].kind;
    mtwnVec4 held = u >= 1.0 ? k[i + 1].value : k[i].value;
    switch (track->kind)
    {
    case mtwn_valueScale:
        return SampleScale(path, i, u, &cubic);
    case mtwn_valueDiscrete:
        return kind == mtwn_segmentStep ? held : (u < 0.5 ? k[i].value : k[i + 1].value);
    case mtwn_valueColor:
    {
        if (kind == mtwn_segmentStep)
        {
            return held;
        }
        mtwnColor color = mtwnMixColorWork(&track->mixes[i].color, u);
        return (mtwnVec4){color.red, color.green, color.blue, color.alpha};
    }
    default:
        break;
    }
    // A rotation, normalized whatever its segment.
    if (kind == mtwn_segmentLinear)
    {
        mtwnQuat q = mtwnMixRotationWork(&track->mixes[i].rotation, u);
        return (mtwnVec4){q.x, q.y, q.z, q.w};
    }
    if (kind == mtwn_segmentStep || u == 1.0)
    {
        Load(held, value);
    }
    else
    {
        double end[4];
        Load(k[i + 1].value, end);
        Evaluate(&cubic, end, u, 4, value, NULL);
    }
    return Normalized(value, k[i].value);
}

static bool MixReads(const mtwnTrack* track, uint32_t i);

// A clip's own track (src/clip_sample.c), made from the keyframes it
// copied: every segment reads, whatever the progress.
MTWN_NOINLINE mtwnVec4 mtwnSampleTrackWork(const mtwnTrack* track, double progress)
{
    MTWN_ASSERT(mtwnValidTrack(track));
    uint32_t i = SegmentAt(&track->path, progress);
    MTWN_ASSERT(Reads(&track->path, i) && MixReads(track, i));
    return TrackIn(track, i, progress);
}

MTWN_NOINLINE bool mtwnValidTrack(const mtwnTrack* track)
{
    if (track == NULL || track->kind > mtwn_valueScale || !mtwnValidPath(&track->path))
    {
        return false;
    }
    const mtwnPath* path = &track->path;
    bool mixed = track->kind == mtwn_valueColor || track->kind == mtwn_valueRotation;
    bool measured = IsNumberKind(track->kind) || track->kind == mtwn_valueScale;
    return path->channels == s_kindChannels[track->kind] &&
           path->logarithmic == (track->kind == mtwn_valueScale) &&
           (track->mixes != NULL) == mixed && (path->lengths == NULL || measured);
}

// Whether a track's sample in segment i may read its mix there, which is
// the host's as its keyframes are: a color's unless the segment steps, a
// rotation's where it is linear.
static bool MixReads(const mtwnTrack* track, uint32_t i)
{
    mtwnSegmentKind kind = track->path.keyframes[i].kind;
    if (track->kind == mtwn_valueColor)
    {
        return kind == mtwn_segmentStep || mtwnValidColorMix(&track->mixes[i].color);
    }
    if (track->kind == mtwn_valueRotation)
    {
        return kind != mtwn_segmentLinear || mtwnValidRotationMix(&track->mixes[i].rotation);
    }
    return true;
}

static MTWN_NOINLINE mtwnResult mtwnSampleTrackChecked(const mtwnTrack* track, double progress,
                                                       mtwnVec4* valueOut)
{
    if (valueOut == NULL || !mtwnValidTrack(track) || !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    uint32_t i = SegmentAt(&track->path, progress);
    if (!Reads(&track->path, i) || !MixReads(track, i))
    {
        return mtwn_errorInvalid;
    }
    *valueOut = TrackIn(track, i, progress);
    return mtwn_success;
}

mtwnResult mtwnSampleTrack(const mtwnTrack* track, double progress, mtwnVec4* valueOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSampleTrackChecked(track, progress, valueOut);
    mtwnFloatEnvLeave(env);
    return result;
}
