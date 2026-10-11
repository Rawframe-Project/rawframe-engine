#!/usr/bin/env python3
# Writes Reach's knock (D660): a block of wood struck, a fifth of a second,
# made here from nothing but decaying sines and a burst of noise, so the
# game carries no one else's recording. Two of the block's modes ring and
# fade fast, struck by a click of noise a few milliseconds long. Mono,
# 24,000 frames a second, 16-bit, its peak at -6 dBFS. Run again only to
# change the knock; its output is committed.
#
# usage: tools/make_knock.py <out.wav>

import math
import random
import struct
import sys
import wave

RATE = 24000
LENGTH = int(0.2 * RATE)

# Each mode's frequency, level, and how fast it fades (per second).
MODES = [(540.0, 1.0, 28.0), (1290.0, 0.55, 45.0), (2210.0, 0.25, 70.0)]


def main():
    random.seed(660)
    samples = []
    for at in range(LENGTH):
        seconds = at / RATE
        value = sum(level * math.exp(-fade * seconds) * math.sin(2 * math.pi * hertz * seconds)
                    for hertz, level, fade in MODES)
        # The strike: noise over the first four milliseconds.
        if seconds < 0.004:
            value += 0.8 * (1 - seconds / 0.004) * (random.random() * 2 - 1)
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
