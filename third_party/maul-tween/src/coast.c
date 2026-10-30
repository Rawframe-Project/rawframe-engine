// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A transition's decay on one channel, out of line:
// the update's loop calls it rather than copying it for each kind.

#include "coast.h"

#include "work.h"

#include <math.h>

// The bounded decay a channel that hands over runs: its bound on the
// side it moves to, or on both sides for one starting past it, as the
// bounded decay chose it from the def's bounds.
mtwnBoundedDecay mtwnCoastBounds(const mtwnPlayerSlot* slot, uint8_t i, double bound,
                                 double velocity)
{
    mtwnBoundedDecay bounded = {slot->coast.decay, slot->coast.bounce, bound, bound};
    if ((slot->crosses & (1u << i)) != 0)
    {
        bounded.lower = velocity > 0.0 ? -HUGE_VAL : bound;
        bounded.upper = velocity > 0.0 ? bound : HUGE_VAL;
    }
    return bounded;
}

// The channel's position and velocity seconds after the start.
void mtwnCoastAt(const mtwnPlayerSlot* slot, uint8_t i, double start, double bound, double velocity,
                 double seconds, double* positionOut, double* velocityOut)
{
    if (slot->lands)
    {
        mtwnDecay own = {slot->rates[i]};
        mtwnSampleDecayWork(&own, start, velocity, seconds, positionOut, velocityOut);
        return;
    }
    if (((slot->crosses | slot->past) & (1u << i)) != 0)
    {
        mtwnBoundedDecay bounded = mtwnCoastBounds(slot, i, bound, velocity);
        mtwnSampleBoundedDecayWork(&bounded, start, velocity, seconds, positionOut, velocityOut);
        return;
    }
    mtwnSampleDecayWork(&slot->decay, start, velocity, seconds, positionOut, velocityOut);
}
