// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keeping decaying states out of the subnormal range, where each
// multiply on a value is many times slower: a recursive filter, an
// envelope or a delay line's contents get there within seconds of
// silence. A state below the floor is set to zero (where it was going;
// a floor would add an offset), checked as rarely as is safe: once a
// block where a block cannot take it from the floor below the normal
// range, or where a value stuck there for the rest of one block costs
// little; on every sample only where neither holds.

#ifndef MAUL_AUDIO_SRC_FLUSH_H
#define MAUL_AUDIO_SRC_FLUSH_H

#include <math.h>
#include <stdint.h>

// The smallest magnitude a decaying state keeps.
#define MAUD_TINY 1e-30

static inline float maudFlush(float x)
{
    return fabsf(x) < (float)MAUD_TINY ? 0.0f : x;
}

static inline double maudFlushDouble(double x)
{
    return fabs(x) < MAUD_TINY ? 0.0 : x;
}

// Flushes count floats in place.
static inline void maudFlushAll(float* values, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        values[i] = maudFlush(values[i]);
    }
}

#endif // MAUL_AUDIO_SRC_FLUSH_H
