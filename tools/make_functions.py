#!/usr/bin/env python3
# Writes the engine's functions (D711): SPEC-0026's standard subgraphs, each
# a function document over the atomic core, into modules/material/library,
# which the material module embeds. A node of a function's type calls it.
# Change one here and run this again; its output is committed.
#
# usage: tools/make_functions.py <modules/material/library>

import json
import math
import os
import sys


class Function:
    def __init__(self, prefix):
        self.prefix = prefix
        self.nodes = {}
        self.count = 0

    def node(self, kind, params=None, inputs=None):
        self.count += 1
        name = f"{self.prefix}{self.count:04x}"
        # A record names its inputs in name order.
        self.nodes[name] = {"type": f"rawframe/{kind}@1", "params": params or {},
                            "inputs": dict(sorted((inputs or {}).items()))}
        return name

    def input(self, name):
        return out(self.node("parameter", {"name": name}))

    def output(self, name, value):
        self.node("output", {"name": name}, {"in": value})

    def text(self, inputs, outputs):
        interface = {}
        if inputs:
            interface["inputs"] = dict(sorted(inputs.items()))
        interface["outputs"] = dict(sorted(outputs.items()))
        document = {"formatVersion": 1, "kind": "function", "interface": interface, "graph": self.nodes}
        return json.dumps(document, indent=2) + "\n"


def out(name, output="out"):
    return {"node": name, "output": output}


def value(type_, default):
    return {"default": default, "type": type_}


def stream(type_, name):
    return {"stream": name, "type": type_}


def fresnel():
    # Schlick's reflectance at the angle between the normal and the way to
    # the eye, for an index of refraction from vacuum.
    f = Function("f7e5000000a0")
    normal, view, ior = f.input("normal"), f.input("view"), f.input("ior")
    facing = out(f.node("saturate", inputs={"in": out(f.node("dot", inputs={"a": normal, "b": view}))}))
    grazing = out(f.node("power", inputs={"a": out(f.node("one_minus", inputs={"in": facing})), "b": 5}))
    ratio = out(f.node("divide", inputs={"a": out(f.node("subtract", inputs={"a": ior, "b": 1})),
                                         "b": out(f.node("add", inputs={"a": ior, "b": 1}))}))
    normal_reflectance = out(f.node("multiply", inputs={"a": ratio, "b": ratio}))
    f.output("out", out(f.node("mix", inputs={"a": normal_reflectance, "b": 1, "t": grazing})))
    return f.text({"ior": value("float", 1.5), "normal": stream("vec3", "world_normal"),
                   "view": stream("vec3", "view_direction")}, {"out": {"type": "float"}})


def tile_and_offset():
    f = Function("f7e5000000b0")
    uv, tiling, offset = f.input("uv"), f.input("tiling"), f.input("offset")
    f.output("out", out(f.node("add", inputs={"a": out(f.node("multiply", inputs={"a": uv, "b": tiling})),
                                              "b": offset})))
    return f.text({"offset": value("vec2", [0, 0]), "tiling": value("vec2", [1, 1]), "uv": stream("vec2", "uv")},
                  {"out": {"type": "vec2"}})


def rotate_uv():
    # About the center, by the angle in radians, counterclockwise.
    f = Function("f7e5000000c0")
    uv, center, angle = f.input("uv"), f.input("center"), f.input("angle")
    away = f.node("subtract", inputs={"a": uv, "b": center})
    parts = f.node("separate2", inputs={"in": out(away)})
    x, y = out(parts, "x"), out(parts, "y")
    cos, sin = out(f.node("cos", inputs={"in": angle})), out(f.node("sin", inputs={"in": angle}))
    turned_x = f.node("subtract", inputs={"a": out(f.node("multiply", inputs={"a": x, "b": cos})),
                                          "b": out(f.node("multiply", inputs={"a": y, "b": sin}))})
    turned_y = f.node("add", inputs={"a": out(f.node("multiply", inputs={"a": x, "b": sin})),
                                     "b": out(f.node("multiply", inputs={"a": y, "b": cos}))})
    turned = f.node("combine2", inputs={"x": out(turned_x), "y": out(turned_y)})
    f.output("out", out(f.node("add", inputs={"a": out(turned), "b": center})))
    return f.text({"angle": value("float", 0), "center": value("vec2", [0.5, 0.5]), "uv": stream("vec2", "uv")},
                  {"out": {"type": "vec2"}})


def polar_coordinates():
    # The distance from the center, and the angle round it in turns, from
    # nought to one, nought along negative x.
    f = Function("f7e5000000d0")
    uv, center = f.input("uv"), f.input("center")
    away = f.node("subtract", inputs={"a": uv, "b": center})
    parts = f.node("separate2", inputs={"in": out(away)})
    radius = out(f.node("length", inputs={"in": out(away)}))
    angle = out(f.node("atan2", inputs={"x": out(parts, "x"), "y": out(parts, "y")}))
    turns = out(f.node("add", inputs={"a": out(f.node("multiply", inputs={"a": angle,
                                                                          "b": round(0.5 / math.pi, 7)})),
                                      "b": 0.5}))
    f.output("out", out(f.node("combine2", inputs={"x": radius, "y": turns})))
    return f.text({"center": value("vec2", [0.5, 0.5]), "uv": stream("vec2", "uv")}, {"out": {"type": "vec2"}})


def checkerboard():
    # Squares of the two colors, `tiling` of them across the coordinates'
    # unit square each way, color_a's at the origin.
    f = Function("f7e5000000e0")
    uv, tiling = f.input("uv"), f.input("tiling")
    cell = f.node("floor", inputs={"in": out(f.node("multiply", inputs={"a": uv, "b": tiling}))})
    parts = f.node("separate2", inputs={"in": out(cell)})
    sum_ = out(f.node("add", inputs={"a": out(parts, "x"), "b": out(parts, "y")}))
    odd = out(f.node("multiply", inputs={"a": out(f.node("fract", inputs={"in": out(f.node(
        "multiply", inputs={"a": sum_, "b": 0.5}))})), "b": 2}))
    f.output("out", out(f.node("mix", inputs={"a": f.input("color_a"), "b": f.input("color_b"), "t": odd})))
    return f.text({"color_a": value("color3", [1, 1, 1]), "color_b": value("color3", [0, 0, 0]),
                   "tiling": value("vec2", [8, 8]), "uv": stream("vec2", "uv")}, {"out": {"type": "color3"}})


def fractal_noise_2d():
    # Fractal Brownian motion over simplex noise, MaterialX's fractal's
    # shape: `octaves` of noise, each `lacunarity` times finer and
    # `diminish` times fainter than the last, `amplitude` the first's. With
    # no loops in a graph, eight are made and those past `octaves` weighed
    # nought; a fraction of an octave fades it in.
    f = Function("f7e5000000f0")
    uv, octaves = f.input("uv"), f.input("octaves")
    lacunarity, diminish = f.input("lacunarity"), f.input("diminish")
    total = None
    scale = None
    weight = f.input("amplitude")
    for octave in range(8):
        at = uv if scale is None else out(f.node("multiply", inputs={"a": uv, "b": scale}))
        noise = out(f.node("simplex_noise_2d", inputs={"in": at}))
        # How much of this octave there is: one below `octaves`, its fraction
        # at it, nought past it.
        share = out(f.node("saturate", inputs={"in": out(f.node("subtract", inputs={"a": octaves, "b": octave}))}))
        term = out(f.node("multiply", inputs={"a": out(f.node("multiply", inputs={"a": noise, "b": weight})),
                                              "b": share}))
        total = term if total is None else out(f.node("add", inputs={"a": total, "b": term}))
        scale = lacunarity if scale is None else out(f.node("multiply", inputs={"a": scale, "b": lacunarity}))
        weight = out(f.node("multiply", inputs={"a": weight, "b": diminish}))
    f.output("out", total)
    return f.text({"amplitude": value("float", 1), "diminish": value("float", 0.5),
                   "lacunarity": value("float", 2), "octaves": value("float", 3), "uv": stream("vec2", "uv")},
                  {"out": {"type": "float"}})


def voronoi_2d():
    # The distance to the nearest feature point, one a cell, each placed in
    # its cell by two of the cell's hashes times `jitter`, MaterialX's
    # worley's shape; the nine cells round the point's searched, unrolled.
    f = Function("f7e500000100")
    uv, jitter = f.input("uv"), f.input("jitter")
    base = out(f.node("floor", inputs={"in": uv}))
    nearest = None
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cell = out(f.node("add", inputs={"a": base, "b": [dx, dy]}))
            # Two hashes of the cell: its own, and its neighbor's far off.
            first = out(f.node("cell_noise_2d", inputs={"in": cell}))
            second = out(f.node("cell_noise_2d", inputs={"in": out(f.node("add", inputs={"a": cell,
                                                                                        "b": [173, 911]}))}))
            spread = out(f.node("combine2", inputs={"x": first, "y": second}))
            # From the cell's center, jittered toward its edges.
            offset = out(f.node("multiply", inputs={"a": out(f.node("subtract", inputs={"a": spread, "b": 0.5})),
                                                    "b": jitter}))
            point = out(f.node("add", inputs={"a": out(f.node("add", inputs={"a": cell, "b": 0.5})), "b": offset}))
            gap = out(f.node("distance", inputs={"a": uv, "b": point}))
            nearest = gap if nearest is None else out(f.node("min", inputs={"a": nearest, "b": gap}))
    f.output("out", nearest)
    return f.text({"jitter": value("float", 1), "uv": stream("vec2", "uv")}, {"out": {"type": "float"}})


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/make_functions.py <modules/material/library>")
    for name, make in (("checkerboard", checkerboard), ("fractal_noise_2d", fractal_noise_2d),
                       ("fresnel", fresnel), ("polar_coordinates", polar_coordinates), ("rotate_uv", rotate_uv),
                       ("tile_and_offset", tile_and_offset), ("voronoi_2d", voronoi_2d)):
        with open(os.path.join(sys.argv[1], name + ".function"), "w", encoding="utf-8") as file:
            file.write(make())


if __name__ == "__main__":
    main()
