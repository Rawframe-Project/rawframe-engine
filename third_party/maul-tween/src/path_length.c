// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A path segment's arc length: adaptive Gauss-Kronrod,
// 15 points against 7, split at the speed's minima; one channel's exact by
// its total variation; and the parameter at a length by safeguarded
// Halley's steps.

#include "path_length.h"

#include "elementary.h"
#include "invariant.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

// Two quadrature orders agreeing to this, relative, end the splitting:
// the inverse's own tolerance. Where a piece ends at a sharp minimum of
// the speed the agreement is about the error itself, not orders above
// it, so a looser one let lengths be off by about it.
static const double s_lengthTolerance = 1e-12;
// The intervals the splitting may hold at once, and the narrowest it
// splits, as a share of the segment.
enum
{
    s_stackSize = 64,
    // The rule pairs one length may take: where the speed falls to 0 at an
    // end, its rounding keeps the two orders apart at every width, and a
    // span too narrow for the share below (2^-40 of the segment by its
    // end) would split until the stack is full, about 2^32 times. The
    // tests' lengths take at most 17. The intervals the cap accepts lie
    // where the speed is its own rounding, so a capped length is off by
    // that rounding over a span this narrow: 4.7e-19 of the segment's
    // length at worst, measured.
    s_mostRules = 1024
};
static const double s_narrowest = 0x1p-30;
// A Halley step this short in u, at a speed at least this share of the
// segment's length, is measured by the length's Taylor series.
static const double s_taylorStep = 1e-4;
static const double s_taylorSpeed = 1e-3;
// Newton on the length stops within this share of the segment's length.
static const double s_inverseTolerance = 1e-12;
enum
{
    s_newtonSteps = 40
};

// Gauss-Kronrod on [-1, 1] (QUADPACK's qk15): the 15-point rule's
// nonnegative abscissas, its weights and the 7-point Gauss rule's on the
// odd ones (0 on the others), the center last.
static const double s_kronrod[8][3] = {
    {0.991455371120812639206854697526329, 0.022935322010529224963732008058970, 0.0},
    {0.949107912342758524526189684047851, 0.063092092629978553290700663189204,
     0.129484966168869693270611432679082},
    {0.864864423359769072789712788640926, 0.104790010322250183839876322541518, 0.0},
    {0.741531185599394439863864773280788, 0.140653259715525918745189590510238,
     0.279705391489276667901467771423780},
    {0.586087235467691130294144845693013, 0.169004726639267902826583426598550, 0.0},
    {0.405845151377397166906606412076961, 0.190350578064785409913256402421014,
     0.381830050505118944950369775488975},
    {0.207784955007898467600689403773245, 0.204432940075298892414161999234649, 0.0},
    {0.0, 0.209482141084727828012999174891714, 0.417959183673469387755102040816327},
};

// The speed at u and its first two derivatives in u: with v = B'(u),
// a = B''(u) and j = B'''(u), s = |v|, s' = v.a / s and
// s'' = (a.a + v.j - s'^2) / s.
static double SpeedChanges(const mtwnCubic* cubic, double u, uint8_t channels, double* firstOut,
                           double* secondOut)
{
    double vv = 0.0;
    double va = 0.0;
    double aa = 0.0;
    double vj = 0.0;
    for (uint8_t c = 0; c < channels; ++c)
    {
        double v = cubic->b[c] + u * (2.0 * cubic->c[c] + u * 3.0 * cubic->d[c]);
        double a = 2.0 * cubic->c[c] + u * 6.0 * cubic->d[c];
        double j = 6.0 * cubic->d[c];
        vv += v * v;
        va += v * a;
        aa += a * a;
        vj += v * j;
    }
    double speed = mtwnRootOfSquares(vv);
    double first = speed > 0.0 ? va / speed : 0.0;
    *firstOut = first;
    *secondOut = speed > 0.0 ? (aa + vj - first * first) / speed : 0.0;
    return speed;
}

// The length from a to b by the 15-point Kronrod rule, and by the 7-point
// Gauss rule on its odd abscissas.
static void Kronrod(const mtwnCubic* cubic, uint8_t channels, double a, double b, double* fine,
                    double* coarse)
{
    double middle = 0.5 * (a + b);
    double half = 0.5 * (b - a);
    // About the middle the derivative is v + w t + j t^2: a pair of
    // abscissas shares its even part.
    double v[4];
    double w[4];
    double j[4];
    double sum = 0.0;
    for (uint8_t c = 0; c < channels; ++c)
    {
        v[c] = cubic->b[c] + middle * (2.0 * cubic->c[c] + middle * 3.0 * cubic->d[c]);
        w[c] = 2.0 * cubic->c[c] + middle * 6.0 * cubic->d[c];
        j[c] = 3.0 * cubic->d[c];
        sum += v[c] * v[c];
    }
    double center = mtwnRootOfSquares(sum);
    double k = s_kronrod[7][1] * center;
    double g = s_kronrod[7][2] * center;
    for (int i = 0; i < 7; ++i)
    {
        double t = half * s_kronrod[i][0];
        double tt = t * t;
        double below = 0.0;
        double above = 0.0;
        for (uint8_t c = 0; c < channels; ++c)
        {
            double even = v[c] + j[c] * tt;
            double odd = w[c] * t;
            below += (even - odd) * (even - odd);
            above += (even + odd) * (even + odd);
        }
        double pair = mtwnRootOfSquares(below) + mtwnRootOfSquares(above);
        k += s_kronrod[i][1] * pair;
        g += s_kronrod[i][2] * pair;
    }
    *fine = half * k;
    *coarse = half * g;
}

// The roots of a number's cubic's derivative, b + 2cu + 3du^2, in
// order: two, one or none.
static int DerivativeRoots(const mtwnCubic* cubic, double roots[2])
{
    double qa = 3.0 * cubic->d[0];
    double qb = 2.0 * cubic->c[0];
    double qc = cubic->b[0];
    if (qa == 0.0)
    {
        roots[0] = qb != 0.0 ? -qc / qb : 0.0;
        return qb != 0.0 ? 1 : 0;
    }
    double disc = qb * qb - 4.0 * qa * qc;
    if (!(disc > 0.0))
    {
        return 0;
    }
    // The stable pair: one root from the sum, the other from it.
    double q = -0.5 * (qb + (qb >= 0.0 ? sqrt(disc) : -sqrt(disc)));
    double r1 = q / qa;
    double r2 = q != 0.0 ? qc / q : r1;
    roots[0] = r1 < r2 ? r1 : r2;
    roots[1] = r1 < r2 ? r2 : r1;
    return 2;
}

// A number's cubic's length from a to b: its total variation, the
// changes between the turns where its derivative is 0. Exact, where a
// quadrature misses the kink each turn puts in |x'|.
static double TurnsLength(const mtwnCubic* cubic, double a, double b)
{
    double roots[2] = {0.0, 0.0};
    int count = DerivativeRoots(cubic, roots);
    double at[4] = {a, a, a, b};
    int turns = 0;
    for (int r = 0; r < count; ++r)
    {
        at[1 + turns] = roots[r];
        turns += roots[r] > a && roots[r] < b ? 1 : 0;
    }
    at[1 + turns] = b;
    double total = 0.0;
    double from = cubic->a[0] + a * (cubic->b[0] + a * (cubic->c[0] + a * cubic->d[0]));
    for (int k = 1; k <= 1 + turns; ++k)
    {
        double u = at[k];
        double to = cubic->a[0] + u * (cubic->b[0] + u * (cubic->c[0] + u * cubic->d[0]));
        total += fabs(to - from);
        from = to;
    }
    return total;
}

// v.a at u, its coefficients in g.
static double SlopeAt(const double g[4], double u)
{
    return g[0] + u * (g[1] + u * (g[2] + u * g[3]));
}

// Where v.a rises through 0 between lo and hi: Newton's steps kept within
// the bracket, halving it where one would leave, until a step is under
// the doubles' spacing at 1.
static double Rise(const double g[4], double lo, double hi)
{
    double u = 0.5 * (lo + hi);
    for (int step = 0; step < 64; ++step)
    {
        double value = SlopeAt(g, u);
        lo = value < 0.0 ? u : lo;
        hi = value < 0.0 ? hi : u;
        double next = u - value / (g[1] + u * (2.0 * g[2] + u * 3.0 * g[3]));
        next = next > lo && next < hi ? next : 0.5 * (lo + hi);
        if (fabs(next - u) <= 0x1p-52)
        {
            return next;
        }
        u = next;
    }
    return u;
}

mtwnMinima mtwnSpeedMinima(const mtwnCubic* cubic, uint8_t channels)
{
    mtwnMinima minima = {{0.0, 0.0}, 0};
    if (channels == 1)
    {
        // Its turns are exact by its total variation.
        return minima;
    }
    // v = b + 2cu + 3du^2 and a = 2c + 6du.
    double g[4] = {0.0, 0.0, 0.0, 0.0};
    for (uint8_t c = 0; c < channels; ++c)
    {
        g[0] += 2.0 * cubic->b[c] * cubic->c[c];
        g[1] += 6.0 * cubic->b[c] * cubic->d[c] + 4.0 * cubic->c[c] * cubic->c[c];
        g[2] += 18.0 * cubic->c[c] * cubic->d[c];
        g[3] += 18.0 * cubic->d[c] * cubic->d[c];
    }
    // Its own turns split (0, 1) into pieces where it is monotone; a piece
    // rising through 0 holds one minimum.
    mtwnCubic slope = {.b = {g[1]}, .c = {g[2]}, .d = {g[3]}};
    double ends[4] = {0.0, 1.0, 1.0, 1.0};
    int count = 1;
    double turns[2] = {0.0, 0.0};
    int found = DerivativeRoots(&slope, turns);
    for (int r = 0; r < found; ++r)
    {
        ends[count] = turns[r];
        count += turns[r] > 0.0 && turns[r] < 1.0 ? 1 : 0;
    }
    ends[count] = 1.0;
    for (int k = 0; k < count; ++k)
    {
        double lo = ends[k];
        double hi = ends[k + 1];
        if (SlopeAt(g, lo) < 0.0 && SlopeAt(g, hi) > 0.0)
        {
            minima.at[minima.count++] = Rise(g, lo, hi);
        }
    }
    return minima;
}

// The arc length of the cubic from a to b, a < b, split at its speed's
// minima inside.
double mtwnArcLength(const mtwnCubic* cubic, uint8_t channels, const mtwnMinima* minima, double a,
                     double b)
{
    // Only constant speed measures, which refuses a step (mtwnMakePath).
    MTWN_ASSERT(!cubic->step);
    if (channels == 1)
    {
        return TurnsLength(cubic, a, b);
    }
    double stack[s_stackSize][2];
    int top = 0;
    double end = b;
    for (int m = minima->count - 1; m >= 0; --m)
    {
        if (minima->at[m] > a && minima->at[m] < end)
        {
            stack[top][0] = minima->at[m];
            stack[top][1] = end;
            end = minima->at[m];
            ++top;
        }
    }
    stack[top][0] = a;
    stack[top][1] = end;
    ++top;
    double total = 0.0;
    double narrowest = (b - a) * s_narrowest;
    int rules = 0;
    while (top > 0)
    {
        --top;
        double lo = stack[top][0];
        double hi = stack[top][1];
        double fine = 0.0;
        double coarse = 0.0;
        Kronrod(cubic, channels, lo, hi, &fine, &coarse);
        rules += 1;
        // A length not finite agrees: no split makes it finite. A path's
        // own keyframes, floats, give none; ones a host changed to NaN would
        // otherwise split to the cap in every length.
        bool agreed = !(fabs(fine - coarse) > s_lengthTolerance * fine);
        if (agreed || hi - lo <= narrowest || top + 2 > s_stackSize || rules >= s_mostRules)
        {
            total += fine;
            continue;
        }
        double middle = 0.5 * (lo + hi);
        stack[top][0] = middle;
        stack[top][1] = hi;
        stack[top + 1][0] = lo;
        stack[top + 1][1] = middle;
        top += 2;
    }
    return total;
}

// The parameter at a length into a segment of that total, by Newton on
// the length, kept within a shrinking bracket.
double mtwnParameterAt(const mtwnCubic* cubic, uint8_t channels, double target, double total)
{
    if (target <= 0.0 || target >= total)
    {
        // Outside, the segment extends by its parameter.
        return target / total;
    }
    double lo = 0.0;
    double hi = 1.0;
    double u = target / total;
    mtwnMinima minima = mtwnSpeedMinima(cubic, channels);
    // Measured from the nearer end, the shorter way.
    double at = u <= 0.5 ? mtwnArcLength(cubic, channels, &minima, 0.0, u)
                         : total - mtwnArcLength(cubic, channels, &minima, u, 1.0);
    for (int step = 0; step < s_newtonSteps; ++step)
    {
        double error = at - target;
        // An error not finite ends it too, as no step can mend it.
        if (!(fabs(error) > s_inverseTolerance * total))
        {
            break;
        }
        if (error < 0.0)
        {
            lo = u;
        }
        else
        {
            hi = u;
        }
        // Halley's step: the length's derivatives in u are the speed and
        // its own.
        double turn = 0.0;
        double bend = 0.0;
        double speed = SpeedChanges(cubic, u, channels, &turn, &bend);
        double denominator = 2.0 * speed * speed - error * turn;
        double next = speed > 0.0 && denominator > 0.0 ? u - 2.0 * error * speed / denominator
                                                       : 0.5 * (lo + hi);
        if (!(next > lo && next < hi))
        {
            next = 0.5 * (lo + hi);
        }
        double h = next - u;
        bool kinked = false;
        for (int m = 0; m < minima.count; ++m)
        {
            kinked |= (minima.at[m] - u) * (minima.at[m] - next) < 0.0;
        }
        if (fabs(h) <= s_taylorStep && speed >= s_taylorSpeed * total && !kinked)
        {
            // A step this short, away from a cusp: the length's Taylor
            // series to h^3, its remainder about h^4 times the speed's third
            // derivative, far below the tolerance.
            at += h * (speed + h * (0.5 * turn + h * bend / 6.0));
        }
        else
        {
            at += next > u ? mtwnArcLength(cubic, channels, &minima, u, next)
                           : -mtwnArcLength(cubic, channels, &minima, next, u);
        }
        u = next;
    }
    return u;
}
