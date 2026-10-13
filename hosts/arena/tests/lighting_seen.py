#!/usr/bin/env python3
# The Lighting Test drawn (D749): runs the arena on a configuration that
# draws the fixture offscreen and writes its last frame, prints what it
# logged, and reads the frame for each light the fixture lights with. Each
# measure was taken with its lamps on and off: the red lamp's pixels
# (red past 1.3 times green and blue: none without it, about 2,000 with),
# the green's (green past 1.1 times red and blue: none, about 850), the
# blue's on the ground below the horizon (blue past 1.3 times red: about
# 4,700, about 18,800), and what the spot light and the glowing block
# make nearly white. A machine with no adapter the configuration allows
# skips, unless RAWFRAME_REQUIRE_GPU is set (as runners_drawn.sh).
#
# usage: lighting_seen.py <rawframe-arena> <configuration> <capture>

import os
import struct
import subprocess
import sys


def counts(path):
    data = open(path, "rb").read()
    width, height = struct.unpack("<HH", data[12:16])
    step = data[16] // 8
    from_top = data[17] & 0x20
    pixels = data[18 + data[0]:]
    found = {"red": 0, "green": 0, "blue": 0, "white": 0}
    for row in range(height):
        down = row if from_top else height - 1 - row
        for column in range(width):
            at = (row * width + column) * step
            b, g, r = pixels[at], pixels[at + 1], pixels[at + 2]
            if r > 60 and r > 1.3 * g and r > 1.3 * b:
                found["red"] += 1
            if g > 80 and g > 1.1 * r and g > 1.1 * b:
                found["green"] += 1
            if down > 0.6 * height and b > 60 and b > 1.3 * r and b > 1.05 * g:
                found["blue"] += 1
            if min(r, g, b) >= 220:
                found["white"] += 1
    return found


def main():
    arena, configuration, capture = sys.argv[1:4]
    if os.path.exists(capture):
        os.remove(capture)
    run = subprocess.run([arena, "--config", configuration], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log = run.stdout.decode("utf-8", "replace")
    if '"device_unavailable"' in log and not os.environ.get("RAWFRAME_REQUIRE_GPU"):
        print("skip: no device")
        return 0
    print(log)
    if run.returncode != 0 or not os.path.exists(capture):
        print("the arena did not draw the fixture")
        return 1
    found = counts(capture)
    print("seen: red %(red)d, green %(green)d, blue %(blue)d, white %(white)d" % found)
    if found["red"] >= 500 and found["green"] >= 200 and found["blue"] >= 10000 and found["white"] >= 500:
        print("lit by every lamp")
        return 0
    print("a lamp's light is missing")
    return 1


sys.exit(main())
