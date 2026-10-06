#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Makes each entry's Metal Shading Language for tools/mrhi_container.py
# --msl, offline, with SPIRV-Cross: it rewrites a copy of the SPIR-V so
# that every binding's descriptor set is 0 and its binding number is the
# index the container's Metal map gives it (docs/contract/container.md),
# then crosses each entry alone to DIR/NAME.metal with SPIRV-Cross's
# --msl-decoration-binding, which takes those numbers as the MSL indices.
# The root block, which has no binding, takes buffer 0. The MSL is made
# for macOS at MSL_VERSION. spirv-cross must be on the path.
#
# usage: mrhi_msl.py SPIRV REFLECTION DIR

import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile

MSL_VERSION = "20300"
STAGES = {"vertex": "vert", "fragment": "frag", "compute": "comp"}


def load_writer():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mrhi_container.py")
    spec = importlib.util.spec_from_file_location("mrhi_container", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def remap(writer, code, bindings):
    """The SPIR-V with each binding's set 0 and its Metal index as its
    binding number."""
    indices = writer.metal_indices(bindings)
    places = {(b["table"], b["slot"]): i for b, i in zip(bindings, indices)}
    writer.need(len(code) >= 20 and len(code) % 4 == 0, "SPIR-V: not a whole module")
    words = list(struct.unpack(f"<{len(code) // 4}I", code))
    writer.need(words[0] == writer.SPIRV_MAGIC, "SPIR-V: not little-endian SPIR-V")
    decorations = {}
    at = 5
    while at < len(words):
        count = words[at] >> 16
        writer.need(count > 0 and at + count <= len(words), "SPIR-V: a damaged instruction")
        if (words[at] & 0xFFFF) == writer.OP_DECORATE and count >= 4:
            target, decoration = words[at + 1], words[at + 2]
            if decoration in (writer.DECORATION_SET, writer.DECORATION_BINDING):
                decorations.setdefault(target, {})[decoration] = at + 3
        at += count
    for target, found in decorations.items():
        set_at = found.get(writer.DECORATION_SET)
        slot_at = found.get(writer.DECORATION_BINDING)
        key = (words[set_at] if set_at is not None else 0,
               words[slot_at] if slot_at is not None else 0)
        writer.need(slot_at is not None and key in places,
                    f"SPIR-V binds {key[0]}.{key[1]}, which the reflection does not")
        if set_at is not None:
            words[set_at] = 0
        words[slot_at] = places[key]
    return struct.pack(f"<{len(words)}I", *words)


def main():
    if len(sys.argv) != 4:
        print("usage: mrhi_msl.py SPIRV REFLECTION DIR", file=sys.stderr)
        return 2
    spirv_path, reflection_path, folder = sys.argv[1:]
    writer = load_writer()
    try:
        with open(spirv_path, "rb") as f:
            code = f.read()
        with open(reflection_path, encoding="utf-8") as f:
            reflection = json.load(f)
        entries = reflection["entries"]
        writer.need(not any(e.get("heap_uses") for e in entries),
                    "Metal reads no heaps yet: a container using one has no Metal code")
        remapped = remap(writer, code, reflection.get("bindings", []))
    except (OSError, ValueError, KeyError, TypeError, writer.ContainerError) as error:
        print(f"mrhi_msl: {error}", file=sys.stderr)
        return 1
    os.makedirs(folder, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        module = os.path.join(work, "remapped.spv")
        with open(module, "wb") as f:
            f.write(remapped)
        for entry in entries:
            result = subprocess.run(["spirv-cross", module, "--msl", "--msl-version", MSL_VERSION,
                                     "--msl-decoration-binding", "--entry", entry["name"],
                                     "--stage", STAGES[entry["stage"]], "--output",
                                     os.path.join(folder, entry["name"] + ".metal")],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                print(f"mrhi_msl: spirv-cross failed on {entry['name']}:\n{result.stdout}"
                      f"{result.stderr}", file=sys.stderr)
                return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
