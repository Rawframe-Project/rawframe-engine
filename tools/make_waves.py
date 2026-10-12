#!/usr/bin/env python3
# Writes Gale's waves (D691): the material that rolls them along the sea,
# the engine's grid, lifting each vertex to the height Gale's float system
# floats its ships on (gale.kest's `height`) at the presented World's time,
# and bending its normal by the waves' slope. The waves' sums here are
# gale.kest's: change one, change both, and run this again. Its output is
# committed.
#
# usage: tools/make_waves.py <games/gale/waves.rfmaterial>

import json
import sys

# Each wave: its height, its number along x or z, its rate, and its phase
# (gale.kest's `height`).
WAVES = ((0.25, "r", 0.3, 1.1, 0.0), (0.15, "b", 0.23, 0.8, 1.3))
# The water's color, linear, and its roughness.
COLOR = (0.012, 0.1, 0.19)
ROUGHNESS = 0.2


def material():
    nodes = {}
    count = [0]

    def node(kind, params=None, inputs=None):
        count[0] += 1
        name = f"5ea0000000000{count[0]:03x}"
        nodes[name] = {"type": f"rawframe/{kind}@1", "params": params or {}, "inputs": inputs or {}}
        return name

    def out(name, output="out"):
        return {"node": name, "output": output}

    time = node("time", {"timeline": "simulation"})
    world = node("world_position")
    axes = node("separate3", inputs={"in": out(world, "position")})
    heights = []
    slopes = []
    for height, axis, number, rate, phase in WAVES:
        along = node("multiply", inputs={"a": out(axes, axis), "b": number})
        moved = node("multiply", inputs={"a": out(time, "seconds"), "b": rate})
        angle = node("add", inputs={"a": out(along), "b": out(moved)})
        if phase != 0:
            angle = node("add", inputs={"a": out(angle), "b": phase})
        heights.append(node("multiply", inputs={"a": out(node("sin", inputs={"in": out(angle)})), "b": height}))
        # The wave's slope along its axis, negated: the normal leans away
        # from the rise.
        slopes.append(node("multiply", inputs={"a": out(node("cos", inputs={"in": out(angle)})),
                                               "b": round(-height * number, 6)}))
    lift = node("add", inputs={"a": out(heights[0]), "b": out(heights[1])})
    offset = node("combine3", inputs={"x": 0, "y": out(lift), "z": 0})
    leaning = node("combine3", inputs={"x": out(slopes[0]), "y": 1, "z": out(slopes[1])})
    normal = node("normalize", inputs={"in": out(leaning)})
    node("surface", inputs={
        "base_color": list(COLOR),
        "geometry_normal": out(normal),
        "position_offset": out(offset),
        "specular_roughness": ROUGHNESS,
    })
    document = {"formatVersion": 1, "kind": "surface", "interface": {}, "graph": nodes}
    return json.dumps(document, indent=2) + "\n"


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/make_waves.py <games/gale/waves.rfmaterial>")
    with open(sys.argv[1], "w", encoding="utf-8") as file:
        file.write(material())


if __name__ == "__main__":
    main()
