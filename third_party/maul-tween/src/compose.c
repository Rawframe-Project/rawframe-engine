// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Composition, Web Animations Level 1's replace and
// add (accumulate is addition for every kind here, as CSS Values 4 says)
// with a weight: the result mixes from the underlying value to the
// composited one. Numbers add and mix per channel; colors premultiplied
// by alpha; rotations multiply (the effect turning in the underlying's
// frame) and mix by slerp; scales compose in their logarithm;
// a discrete value is not additive and steps at half.

#include "compose.h"

#include "bounds_internal.h"
#include "elementary.h"
#include "work.h"

#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

static void Unpack(mtwnVec4 v, double out[4])
{
    out[0] = (double)v.x;
    out[1] = (double)v.y;
    out[2] = (double)v.z;
    out[3] = (double)v.w;
}

static mtwnVec4 Pack(const double v[4])
{
    return mtwnNumbersHeld((mtwnVec4){(float)v[0], (float)v[1], (float)v[2], (float)v[3]});
}

// Per channel: the sum for add, then the mix by the weight, of values and
// velocities in double. Held past the floats, a number is still.
static mtwnMoving Linear(mtwnComposite composite, double weight, mtwnMoving under,
                         mtwnMoving effect)
{
    double a[4];
    double b[4];
    double va[4];
    double vb[4];
    Unpack(under.value, a);
    Unpack(effect.value, b);
    Unpack(under.velocity, va);
    Unpack(effect.velocity, vb);
    double value[4];
    double velocity[4];
    bool add = composite == mtwn_compositeAdd;
    for (int i = 0; i < 4; ++i)
    {
        double composed = add ? a[i] + b[i] : b[i];
        double moved = add ? va[i] + vb[i] : vb[i];
        value[i] = weight == 1.0 ? composed : a[i] + weight * (composed - a[i]);
        velocity[i] = weight == 1.0 ? moved : va[i] + weight * (moved - va[i]);
        velocity[i] = fabs(value[i]) <= 0x1.fffffep127 ? velocity[i] : 0.0;
    }
    return (mtwnMoving){Pack(value), Pack(velocity)};
}

static void Premultiply(mtwnVec4 v, double out[4])
{
    Unpack(v, out);
    for (int i = 0; i < 3; ++i)
    {
        out[i] *= out[3];
    }
}

static mtwnVec4 Color(mtwnComposite composite, double weight, mtwnVec4 under, mtwnVec4 effect)
{
    double a[4];
    double b[4];
    Premultiply(under, a);
    Premultiply(effect, b);
    double out[4];
    for (int i = 0; i < 4; ++i)
    {
        double composed = composite == mtwn_compositeAdd ? a[i] + b[i] : b[i];
        out[i] = a[i] + weight * (composed - a[i]);
    }
    out[3] = out[3] > 1.0 ? 1.0 : out[3];
    // Back to straight alpha; a color of alpha 0 has channels 0, as CSS
    // gives a fully transparent premultiplied mix.
    for (int i = 0; i < 3; ++i)
    {
        out[i] = out[3] > 0.0 ? out[i] / out[3] : 0.0;
    }
    return Pack(out);
}

// The Hamilton product a b: b's turn in a's frame.
static mtwnQuat Multiply(mtwnQuat a, mtwnQuat b)
{
    double ax = (double)a.x;
    double ay = (double)a.y;
    double az = (double)a.z;
    double aw = (double)a.w;
    double bx = (double)b.x;
    double by = (double)b.y;
    double bz = (double)b.z;
    double bw = (double)b.w;
    double x = aw * bx + ax * bw + ay * bz - az * by;
    double y = aw * by - ax * bz + ay * bw + az * bx;
    double z = aw * bz + ax * by - ay * bx + az * bw;
    double w = aw * bw - ax * bx - ay * by - az * bz;
    double norm = sqrt(x * x + y * y + z * z + w * w);
    if (!(norm > 0.0))
    {
        return a;
    }
    return (mtwnQuat){(float)(x / norm), (float)(y / norm), (float)(z / norm), (float)(w / norm)};
}

// The slerp from one rotation toward another by a weight, along the
// shortest arc; the far one itself at full weight.
static mtwnQuat Toward(mtwnQuat from, mtwnQuat to, double weight)
{
    mtwnRotationMix mix;
    if (weight < 1.0 &&
        mtwnMakeRotationMixWork(from, to, mtwn_rotationShortest, &mix) == mtwn_success)
    {
        return mtwnMixRotationWork(&mix, weight);
    }
    return to;
}

// Replace slerps from the underlying rotation toward the effect; add
// slerps from no rotation toward the effect, then turns the underlying by
// that, the effect in the underlying's frame.
static mtwnVec4 Rotation(mtwnComposite composite, double weight, mtwnVec4 under, mtwnVec4 effect)
{
    mtwnQuat a = {under.x, under.y, under.z, under.w};
    mtwnQuat b = {effect.x, effect.y, effect.z, effect.w};
    mtwnQuat composed = composite == mtwn_compositeAdd
                            ? Multiply(a, Toward((mtwnQuat){0.0f, 0.0f, 0.0f, 1.0f}, b, weight))
                            : Toward(a, b, weight);
    return (mtwnVec4){composed.x, composed.y, composed.z, composed.w};
}

// Scales in their logarithm: add multiplies by the effect to the
// weight's power, replace moves toward it by that power of the ratio; the
// velocities by the chain rule, as rates v / s. At full weight replace is
// the effect and add the product, exactly.
static mtwnMoving Scale(mtwnComposite composite, double weight, mtwnMoving under, mtwnMoving effect)
{
    double a = (double)under.value.x;
    double b = (double)effect.value.x;
    double ra = (double)under.velocity.x / a;
    double rb = (double)effect.velocity.x / b;
    bool add = composite == mtwn_compositeAdd;
    double c = 0.0;
    if (weight == 1.0)
    {
        c = add ? a * b : b;
    }
    else
    {
        c = a * mtwnExp(weight * (add ? mtwnLog(b) : mtwnLog(b) - mtwnLog(a)));
    }
    double rate = add ? ra + weight * rb : ra + weight * (rb - ra);
    return (mtwnMoving){{mtwnScaleFloat(c), 0.0f, 0.0f, 0.0f},
                        {mtwnVelocityFloat(c * rate), 0.0f, 0.0f, 0.0f}};
}

mtwnMoving mtwnCompose(mtwnValueKind kind, mtwnComposite composite, double weight,
                       mtwnMoving underlying, mtwnMoving effect)
{
    if (weight == 0.0)
    {
        return underlying;
    }
    if (kind == mtwn_valueDiscrete)
    {
        // Not additive; a weight steps at half, as discrete values mix.
        return weight >= 0.5 ? (mtwnMoving){effect.value, {0.0f, 0.0f, 0.0f, 0.0f}} : underlying;
    }
    if (kind == mtwn_valueScale)
    {
        return Scale(composite, weight, underlying, effect);
    }
    // A color's and a rotation's velocities mix as numbers' do.
    mtwnMoving out = Linear(composite, weight, underlying, effect);
    if (kind == mtwn_valueColor)
    {
        out.value = Color(composite, weight, underlying.value, effect.value);
    }
    else if (kind == mtwn_valueRotation)
    {
        out.value = Rotation(composite, weight, underlying.value, effect.value);
    }
    if (kind == mtwn_valueInteger)
    {
        out.value.x = (float)round((double)out.value.x);
    }
    return out;
}
