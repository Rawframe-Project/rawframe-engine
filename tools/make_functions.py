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


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: tools/make_functions.py <modules/material/library>")
    for name, make in (("checkerboard", checkerboard), ("fresnel", fresnel),
                       ("polar_coordinates", polar_coordinates), ("rotate_uv", rotate_uv),
                       ("tile_and_offset", tile_and_offset)):
        with open(os.path.join(sys.argv[1], name + ".function"), "w", encoding="utf-8") as file:
            file.write(make())


if __name__ == "__main__":
    main()
