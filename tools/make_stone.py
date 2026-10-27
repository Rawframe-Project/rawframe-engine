#!/usr/bin/env python3
# Writes Gale's stone: a small, tiling picture of layered rock,
# light grey strata grained by value noise, which the islands' material
# lays on them along each world axis. Run again only to change the
# picture; its output is committed.
#
# usage: tools/make_stone.py <out.png>

import math
import struct
import sys
import zlib

SIZE = 64


def lattice(x, y, period, seed):
    # A repeatable hash of a lattice point, wrapped so the picture tiles.
    x, y = x % period, y % period
    n = (x * 374761393 + y * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def noise(u, v, period, seed):
    x, y = math.floor(u), math.floor(v)
    fx, fy = u - x, v - y
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    top = lattice(x, y, period, seed) * (1 - sx) + lattice(x + 1, y, period, seed) * sx
    bottom = lattice(x, y + 1, period, seed) * (1 - sx) + lattice(x + 1, y + 1, period, seed) * sx
    return top * (1 - sy) + bottom * sy


def stone(px, py):
    u, v = px / SIZE, py / SIZE
    grain = sum(noise(u * 4 * 2 ** o, v * 4 * 2 ** o, 4 * 2 ** o, o) * 0.5 ** o for o in range(4)) / 1.875
    # Strata across the picture, bent by the grain.
    strata = 0.5 + 0.5 * math.sin((v * 6 + grain * 1.5) * 2 * math.pi)
    shade = 0.62 + 0.25 * grain + 0.13 * strata
    return tuple(max(0, min(255, round(255 * shade * tint))) for tint in (1.0, 0.97, 0.93))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/make_stone.py <out.png>")
    rows = b"".join(b"\x00" + bytes(c for px in range(SIZE) for c in stone(px, py)) for py in range(SIZE))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(sys.argv[1], "wb") as file:
        file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)) +
                   chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
