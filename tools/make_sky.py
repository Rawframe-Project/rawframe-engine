#!/usr/bin/env python3
# Writes a clear day's sky as a Radiance picture (D322): Preetham, Shirley,
# and Smits's analytic daylight (SIGGRAPH 1999) above the horizon, for a
# turbidity and the sun's direction, without the sun's disc, which a game
# lights with its own sun; below it, a level ground giving back its albedo
# of the sun's light and the sky's. Equirectangular, its middle looking
# along -Z and its top +Y, as the texture cook takes it (D321); scaled so
# the sky above the horizon averages one, so a game's Sky luminance is the
# sky's in candela per square meter. Run again only to change the picture;
# its output is committed.
#
# usage: tools/make_sky.py <out.hdr> [width] [turbidity]

import math
import sys

# The sun as plaza's default light has it (toward the sun, normalized), its
# illuminance over the sky's luminance, and the ground's albedo (D304's
# default, sRGB 0x7C).
TO_SUN = (0.3, 1.0, 0.4)
SUN_OVER_SKY = 100000.0 / 5000.0
GROUND = (0.2, 0.19, 0.17)


def perez(theta, gamma, coefficients):
    a, b, c, d, e = coefficients
    return (1 + a * math.exp(b / max(math.cos(theta), 0.01))) * (1 + c * math.exp(d * gamma) + e * math.cos(gamma) ** 2)


def sky_model(turbidity, sun_theta):
    t = turbidity
    luminance = (0.1787 * t - 1.4630, -0.3554 * t + 0.4275, -0.0227 * t + 5.3251, 0.1206 * t - 2.5771,
                 -0.0670 * t + 0.3703)
    x = (-0.0193 * t - 0.2592, -0.0665 * t + 0.0008, -0.0004 * t + 0.2125, -0.0641 * t - 0.8989,
         -0.0033 * t + 0.0452)
    y = (-0.0167 * t - 0.2608, -0.0950 * t + 0.0092, -0.0079 * t + 0.2102, -0.0441 * t - 1.6537,
         -0.0109 * t + 0.0529)
    s = sun_theta
    chi = (4.0 / 9.0 - t / 120.0) * (math.pi - 2 * s)
    zenith_luminance = (4.0453 * t - 4.9710) * math.tan(chi) - 0.2155 * t + 2.4192
    zenith_x = (t * t * (0.00166 * s ** 3 - 0.00375 * s ** 2 + 0.00209 * s) +
                t * (-0.02903 * s ** 3 + 0.06377 * s ** 2 - 0.03202 * s + 0.00394) +
                (0.11693 * s ** 3 - 0.21196 * s ** 2 + 0.06052 * s + 0.25886))
    zenith_y = (t * t * (0.00275 * s ** 3 - 0.00610 * s ** 2 + 0.00317 * s) +
                t * (-0.04214 * s ** 3 + 0.08970 * s ** 2 - 0.04153 * s + 0.00516) +
                (0.15346 * s ** 3 - 0.26756 * s ** 2 + 0.06670 * s + 0.26688))
    zeniths = ((zenith_luminance, luminance), (zenith_x, x), (zenith_y, y))

    def at(theta, gamma):
        big_y, small_x, small_y = (zenith * perez(theta, gamma, c) / perez(0.0, s, c) for zenith, c in zeniths)
        # xyY to XYZ to linear Rec. 709.
        big_x = small_x * big_y / small_y
        big_z = (1 - small_x - small_y) * big_y / small_y
        return (max(3.2406 * big_x - 1.5372 * big_y - 0.4986 * big_z, 0.0),
                max(-0.9689 * big_x + 1.8758 * big_y + 0.0415 * big_z, 0.0),
                max(0.0557 * big_x - 0.2040 * big_y + 1.0570 * big_z, 0.0))

    return at


def rgbe(light):
    largest = max(light)
    if largest < 1e-32:
        return (0, 0, 0, 0)
    mantissa, exponent = math.frexp(largest)
    scale = mantissa * 256.0 / largest
    return tuple(min(int(channel * scale), 255) for channel in light) + (exponent + 128,)


def scanline(texels):
    # The new run-length form: each channel in turn, a run for a repeat of
    # three or more, literals between.
    out = bytearray((2, 2, len(texels) >> 8, len(texels) & 0xFF))
    for channel in range(4):
        values = [texel[channel] for texel in texels]
        at = 0
        while at < len(values):
            run = 1
            while at + run < len(values) and run < 127 and values[at + run] == values[at]:
                run += 1
            if run >= 3:
                out += bytes((128 + run, values[at]))
                at += run
                continue
            start = at
            while at < len(values) and at - start < 128:
                if at + 2 < len(values) and values[at] == values[at + 1] == values[at + 2]:
                    break
                at += 1
            out += bytes((at - start,)) + bytes(values[start:at])
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: tools/make_sky.py <out.hdr> [width] [turbidity]")
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 512
    turbidity = float(sys.argv[3]) if len(sys.argv) > 3 else 2.5
    height = width // 2
    length = math.sqrt(sum(axis * axis for axis in TO_SUN))
    to_sun = tuple(axis / length for axis in TO_SUN)
    sky = sky_model(turbidity, math.acos(to_sun[1]))

    rows = []
    above = 0.0
    weight = 0.0
    for row in range(height):
        latitude = (0.5 - (row + 0.5) / height) * math.pi
        texels = []
        for column in range(width):
            longitude = ((column + 0.5) / width - 0.5) * 2 * math.pi
            toward = (math.sin(longitude) * math.cos(latitude), math.sin(latitude),
                      -math.cos(longitude) * math.cos(latitude))
            if toward[1] >= 0:
                gamma = math.acos(max(-1.0, min(1.0, sum(a * b for a, b in zip(toward, to_sun)))))
                light = sky(math.acos(toward[1]), gamma)
                luminance = 0.2126 * light[0] + 0.7152 * light[1] + 0.0722 * light[2]
                above += luminance * math.cos(latitude)
                weight += math.cos(latitude)
            else:
                light = None
            texels.append(light)
        rows.append(texels)
    scale = weight / above
    # The ground: its albedo of the sun at its height and of the sky above,
    # an even sky's π times its average of one, over π.
    ground = tuple(albedo * (SUN_OVER_SKY * to_sun[1] + math.pi) / math.pi for albedo in GROUND)
    with open(sys.argv[1], "wb") as file:
        file.write(b"#?RADIANCE\n# tools/make_sky.py: Preetham daylight, turbidity %.1f\n" % turbidity)
        file.write(b"FORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (height, width))
        for texels in rows:
            made = [rgbe(ground if light is None else tuple(channel * scale for channel in light)) for light in texels]
            file.write(scanline(made))


if __name__ == "__main__":
    main()
