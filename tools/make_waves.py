#!/usr/bin/env python3
# Writes Gale's waves (D691): the material that rolls them along the sea,
# the engine's grid, lifting each vertex to the height Gale's float system
# floats its ships on (gale.kest's `height`) at the presented World's time,
# and bending its normal by the waves' slope; foam whitens the crests
# (D700). The waves' sums here are
# gale.kest's: change one, change both, and run this again. Its output is
# committed.
#
# usage: tools/make_waves.py <games/gale/waves.rfmaterial>

import json
import math
import sys

# The seconds the sea repeats in (D702): gale.kest's `float` wraps the
# World's time at them, and the material's time node at the same, so both
# read a game days long as finely as one a minute long. Each wave's rate
# turns it a whole number of times in them, and the foam drifts round once.
PERIOD = 600
# Each wave: its height, its number along x or z, its rate, and its phase
# (gale.kest's `height`); 105 and 76 turns a period.
WAVES = ((0.25, "r", 0.3, 1.0995574, 0.0), (0.15, "b", 0.23, 0.7958701, 1.3))
# The water's color, linear, and its roughness; and the foam's, which
# whitens the crests where they rise past FOAM_FROM toward FOAM_TO meters,
# broken up by value noise drifting with the sea (D700), round a circle
# DRIFT across, in noise cells, once a period.
COLOR = (0.012, 0.1, 0.19)
ROUGHNESS = 0.2
FOAM = (0.75, 0.8, 0.82)
FOAM_ROUGHNESS = 0.7
FOAM_FROM = 0.26
FOAM_TO = 0.38
DRIFT = 54


def material():
    nodes = {}
    count = [0]

    def node(kind, params=None, inputs=None):
        count[0] += 1
        name = f"5ea0000000000{count[0]:03x}"
        # A record names its inputs in name order.
        nodes[name] = {"type": f"rawframe/{kind}@1", "params": params or {}, "inputs": dict(sorted((inputs or {}).items()))}
        return name

    def out(name, output="out"):
        return {"node": name, "output": output}

    time = node("time", {"period": PERIOD, "timeline": "simulation"})
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
    # The foam: the crest's height, give or take noise over the water.
    flat = node("combine2", inputs={"x": out(axes, "r"), "y": out(axes, "b")})
    turn = node("multiply", inputs={"a": out(time, "seconds"), "b": round(2 * math.pi / PERIOD, 7)})
    circle = node("combine2", inputs={"x": out(node("cos", inputs={"in": out(turn)})),
                                      "y": out(node("sin", inputs={"in": out(turn)}))})
    drift = node("multiply", inputs={"a": out(circle), "b": DRIFT})
    place = node("add", inputs={"a": out(node("multiply", inputs={"a": out(flat), "b": 1.3})), "b": out(drift)})
    ragged = node("multiply", inputs={"a": out(node("subtract", inputs={
        "a": out(node("value_noise_2d", inputs={"in": out(place)})), "b": 0.5})), "b": 0.16})
    crest = node("add", inputs={"a": out(lift), "b": out(ragged)})
    foam = node("smoothstep", inputs={"low": FOAM_FROM, "high": FOAM_TO, "in": out(crest)})
    node("surface", inputs={
        "base_color": out(node("mix", inputs={"a": list(COLOR), "b": list(FOAM), "t": out(foam)})),
        "geometry_normal": out(normal),
        "position_offset": out(offset),
        "specular_roughness": out(node("mix", inputs={"a": ROUGHNESS, "b": FOAM_ROUGHNESS, "t": out(foam)})),
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
