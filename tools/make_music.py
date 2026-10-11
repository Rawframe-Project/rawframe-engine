#!/usr/bin/env python3
# Writes Runners' music (D629): eight bars at 120 beats a minute, sixteen
# seconds, made here from nothing but sines, triangles, and noise, so the
# game carries no one else's recording. A minor's progression (A minor, F,
# C, G, twice) as a pad, a bass on each beat, an arpeggio in eighths, and a
# soft tick on the off-beats. Every note's tail past the end is laid over
# the start, so the loop goes round without a seam. Mono, 24,000 frames a
# second, 16-bit, its peak at -6 dBFS. Run again only to change the music;
# its output is committed.
#
# usage: tools/make_music.py <out.wav>

import math
import random
import struct
import sys
import wave

RATE = 24000
BEAT = 0.5
BARS = 8
LENGTH = int(BARS * 4 * BEAT * RATE)

# Each bar's chord, as MIDI notes from its root.
CHORDS = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)] * 2


def frequency(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def triangle(phase):
    # From nought at its start, as a sine, so a note starts without a click.
    phase = (phase + 0.25) % 1.0
    return 4 * phase - 1 if phase < 0.5 else 3 - 4 * phase


def add(track, start, samples):
    # Laid in at `start`, what runs past the end coming round to the start.
    for offset, value in enumerate(samples):
        track[(start + offset) % LENGTH] += value


def note(pitch, seconds, level, attack, release, shape):
    count = int((seconds + release) * RATE)
    held = int(seconds * RATE)
    step = frequency(pitch) / RATE
    out = []
    for at in range(count):
        if at < attack * RATE:
            envelope = at / (attack * RATE)
        elif at < held:
            envelope = 1.0
        else:
            envelope = math.exp(-(at - held) / (release * RATE / 4))
        out.append(level * envelope * shape(at * step))
    return out


def pluck(pitch, level):
    step = frequency(pitch) / RATE
    return [level * math.exp(-at / (0.12 * RATE)) * (triangle(at * step) * 0.7 + math.sin(2 * math.pi * at * step) * 0.3)
            for at in range(int(0.6 * RATE))]


def tick(level, noise):
    return [level * math.exp(-at / (0.008 * RATE)) * noise.uniform(-1, 1) for at in range(int(0.05 * RATE))]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/make_music.py <out.wav>")
    track = [0.0] * LENGTH
    noise = random.Random(629)
    bar = int(4 * BEAT * RATE)
    eighth = int(BEAT / 2 * RATE)
    for index, chord in enumerate(CHORDS):
        start = index * bar
        for pitch in chord:
            add(track, start, note(pitch, 4 * BEAT, 0.08, 0.3, 0.6, triangle))
        for beat in range(4):
            add(track, start + beat * int(BEAT * RATE),
                note(chord[0] - 12, BEAT * 0.8, 0.22, 0.01, 0.1, lambda phase: math.sin(2 * math.pi * phase)))
        pattern = [chord[0], chord[1], chord[2], chord[1] + 12, chord[2], chord[1], chord[0] + 12, chord[2]]
        for step, pitch in enumerate(pattern):
            add(track, start + step * eighth, pluck(pitch + 12, 0.1))
            if step % 2 == 1:
                add(track, start + step * eighth, tick(0.05, noise))
    peak = max(abs(value) for value in track)
    scale = 10 ** (-6 / 20) / peak
    with wave.open(sys.argv[1], "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", round(value * scale * 32767)) for value in track))


if __name__ == "__main__":
    main()
