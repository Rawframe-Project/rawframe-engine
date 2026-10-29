// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "web_clock.h"

#include "clock.h"
#include "xrun.h"

// How much later than the held earliest a frame-0 time may come before
// it is frames lost, not the estimate's drift: twice the largest drift
// measured through PulseAudio.
#define MAUD_WEB_LOSS_NANOSECONDS 50000000LL

// How long a step must last to be a loss: a span. A delivery the browser
// places late moves one frame-0 time and the next ones back; lost frames
// move every one after them.
#define MAUD_WEB_STEP_NANOSECONDS (MAUD_WEB_CLOCK_SPAN_SECONDS * 1000000000LL)

void maudWebResetClock(maudWebClock* clock)
{
    for (int i = 0; i < MAUD_WEB_CLOCK_SPANS; ++i)
    {
        clock->span[i] = INT64_MIN;
    }
    clock->stepSince = INT64_MIN;
    clock->recentCount = 0;
    clock->recentNext = 0;
}

// Takes a capture's frame-0 time and returns the median of the last
// MAUD_WEB_CLOCK_MEDIAN, the upper of the two middle ones while it holds
// an even count: a single low never passes, even as the window fills.
static int64_t Median(maudWebClock* clock, int64_t origin)
{
    clock->recent[clock->recentNext] = origin;
    clock->recentNext = (clock->recentNext + 1) % MAUD_WEB_CLOCK_MEDIAN;
    clock->recentCount += clock->recentCount < MAUD_WEB_CLOCK_MEDIAN ? 1 : 0;
    int64_t sorted[MAUD_WEB_CLOCK_MEDIAN];
    for (int i = 0; i < clock->recentCount; ++i)
    {
        int j = i;
        for (; j > 0 && sorted[j - 1] > clock->recent[i]; --j)
        {
            sorted[j] = sorted[j - 1];
        }
        sorted[j] = clock->recent[i];
    }
    return sorted[clock->recentCount / 2];
}

// Watches frame-0 times past the bound: true once they have all been
// past it for a step's length, the earliest of them in *earliest.
static bool Lost(maudWebClock* clock, int64_t origin, int64_t least, int64_t now, int64_t* earliest)
{
    if (origin - least <= MAUD_WEB_LOSS_NANOSECONDS)
    {
        clock->stepSince = INT64_MIN;
        return false;
    }
    if (clock->stepSince == INT64_MIN)
    {
        clock->stepSince = now;
        clock->stepEarliest = origin;
    }
    else if (origin < clock->stepEarliest)
    {
        clock->stepEarliest = origin;
    }
    *earliest = clock->stepEarliest;
    return now - clock->stepSince >= MAUD_WEB_STEP_NANOSECONDS;
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
        int64_t origin = output ? now + latency - place : Median(clock, now - latency - place);
        // Until a capture has enough deliveries for their median to mean
        // anything, its clock follows the median and the filter, which
        // would hold a low for 30 seconds, takes none of them.
        int64_t least = origin;
        if (output || clock->recentCount >= MAUD_WEB_CLOCK_SETTLED)
        {
            least = maudWebEarliest(clock, origin, now);
            int64_t earliest = 0;
            if (Lost(clock, origin, least, now, &earliest))
            {
                // Frame 0 later for good: the browser lost frames before
                // the stream got them (or played silence it never asked
                // for).
                maudCountXrun(core);
                maudWebResetClock(clock);
                (void)maudWebEarliest(clock, earliest, now);
                least = maudWebEarliest(clock, origin, now);
            }
        }
        int64_t filtered = output ? least + place - now : now - least - place;
        latency = filtered > 0 ? filtered : 0;
    }
    maudStampClockAt(core, output ? now + latency : now - latency, latency);
}
