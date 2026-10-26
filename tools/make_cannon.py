#!/usr/bin/env python3
# Writes Gale's broadside: a cannon's report, eight tenths of a
# second, made here from nothing but noise and decaying sines, so the game
# carries no one else's recording. A crack of bright noise opens it, a low
# thump and a rumble of darkened noise follow and fade. Mono, 24,000 frames
# a second, 16-bit, its peak at -6 dBFS. Run again only to change the
# report; its output is committed.
#
# usage: tools/make_cannon.py <out.wav>

import math
import random
import struct
import sys
import wave

RATE = 24000
LENGTH = int(0.8 * RATE)


def main():
    random.seed(686)
    samples = []
    rumble = 0.0
    for at in range(LENGTH):
        seconds = at / RATE
        noise = random.random() * 2 - 1
        # The rumble: noise darkened by a one-pole low pass, fading slowly.
        rumble += 0.04 * (noise - rumble)
        value = 3.0 * rumble * math.exp(-4.5 * seconds)
        # The thump: a falling low sine.
        hertz = 70.0 * math.exp(-1.5 * seconds) + 38.0
        value += 0.9 * math.exp(-7.0 * seconds) * math.sin(2 * math.pi * hertz * seconds)
        # The crack: bright noise over the first twenty milliseconds.
        if seconds < 0.02:
            value += 0.7 * (1 - seconds / 0.02) * noise
        samples.append(value)
    peak = max(abs(value) for value in samples)
    scale = 0.5 / peak * 32767
    with wave.open(sys.argv[1], "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", round(value * scale)) for value in samples))


if __name__ == "__main__":
    main()
