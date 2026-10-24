// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "web_clock.h"

#include "clock.h"
#include "xrun.h"

// How much later than the held earliest a frame-0 time may come before
// it is frames lost, not the estimate's drift: twice the largest drift
// measured through PulseAudio (research in the docs).
#define MAUD_WEB_LOSS_NANOSECONDS 50000000LL

void maudWebResetClock(maudWebClock* clock)
{
    for (int i = 0; i < MAUD_WEB_CLOCK_SPANS; ++i)
    {
        clock->span[i] = INT64_MIN;
    }
}

int64_t maudWebEarliest(maudWebClock* clock, int64_t origin, int64_t now)
{
    int64_t span = now / (MAUD_WEB_CLOCK_SPAN_SECONDS * 1000000000LL);
    int slot = (int)(span % MAUD_WEB_CLOCK_SPANS);
    if (clock->span[slot] != span)
    {
        clock->span[slot] = span;
        clock->earliest[slot] = origin;
    }
    else if (origin < clock->earliest[slot])
    {
        clock->earliest[slot] = origin;
    }
    int64_t least = origin;
    for (int i = 0; i < MAUD_WEB_CLOCK_SPANS; ++i)
    {
        if (clock->span[i] > span - MAUD_WEB_CLOCK_SPANS && clock->earliest[i] < least)
        {
            least = clock->earliest[i];
        }
    }
    return least;
}

void maudWebStampClock(maudWebClock* clock, int* clockEpoch, maudStreamCore* core, int64_t latency,
                       bool stamped, int epoch, bool output)
{
    int64_t now = maudNowNanoseconds();
    uint32_t rate = core->period.sampleRate;
    if (stamped && rate != 0)
    {
        if (epoch != *clockEpoch)
        {
            maudWebResetClock(clock);
            *clockEpoch = epoch;
        }
        uint64_t position = atomic_load_explicit(&core->position, memory_order_relaxed);
        int64_t place = (int64_t)((double)position * 1e9 / rate);
        int64_t origin = output ? now + latency - place : now - latency - place;
        int64_t least = maudWebEarliest(clock, origin, now);
        if (origin - least > MAUD_WEB_LOSS_NANOSECONDS)
        {
            // Frame 0 later for good: the browser lost frames before the
            // stream got them (or played silence it never asked for).
            maudCountXrun(core);
            maudWebResetClock(clock);
            least = maudWebEarliest(clock, origin, now);
        }
        int64_t filtered = output ? least + place - now : now - least - place;
        latency = filtered > 0 ? filtered : 0;
    }
    maudStampClockAt(core, output ? now + latency : now - latency, latency);
}
