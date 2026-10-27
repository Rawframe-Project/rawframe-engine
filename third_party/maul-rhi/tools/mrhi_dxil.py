#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Makes each entry's DXIL for tools/mrhi_container.py --dxil, offline, with
# SPIRV-Cross and DXC, by the writer's D3D12 rule
# (docs/user/shader-container-format.md). SPIRV-Cross already places a binding at
# its binding number as register and its set as space, which is the rule; the
# tool gives a copy of the SPIR-V's root block (its push constants) descriptor
# set 5 and binding 0, so that SPIRV-Cross places it at b0 of space 5. Each
# variable of the heaps (set 4) moves to binding 0 of a set of its own from 16,
# in the order the module declares them, so that arrays of one register class
# never share a register and space. Each entry is crossed alone to HLSL, a vertex
# entry reading the draw's base vertex and first instance from b2 of space 5,
# since D3D12's vertex and instance ids leave them out. Each constant the SPIR-V
# does not size anything with is defined as a read of b1 of space 5, where the
# driver writes the pipeline's values; a fixed one keeps its default. DXC
# compiles each entry for SHADER_MODEL, 6.1 for one reading the view index, 6.2
# with 16-bit types for a module declaring Float16, to DIR/NAME.dxil without
# reflection or debug data. HLSL that reads a loop's variable after its continue
# block has stepped it is refused (stale_reads). spirv-cross and dxc must be on
# the path.
#
# usage: mrhi_dxil.py SPIRV REFLECTION DIR

import importlib.util
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

SHADER_MODEL = "6_0"
STAGES = {"vertex": ("vert", "vs"), "fragment": ("frag", "ps"), "compute": ("comp", "cs")}
OP_VARIABLE = 59
STORAGE_PUSH_CONSTANT = 9
# The annotation instructions, before which the root block's
# decorations go.
ANNOTATIONS = (71, 72, 73, 74, 332, 5632, 5633)
# How HLSL reads a constant's 32 bits, by its type.
READS = {"bool": "({} != 0u)", "int32": "asint({})", "uint32": "{}", "float32": "asfloat({})"}


def load_writer():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mrhi_container.py")
    spec = importlib.util.spec_from_file_location("mrhi_container", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def place_root(writer, code):
    """The SPIR-V with its push constants at the root block's set and
    binding."""
    writer.need(len(code) >= 20 and len(code) % 4 == 0, "SPIR-V: not a whole module")
    words = list(struct.unpack(f"<{len(code) // 4}I", code))
    writer.need(words[0] == writer.SPIRV_MAGIC, "SPIR-V: not little-endian SPIR-V")
    first = None
    roots = []
    at = 5
    while at < len(words):
        count = words[at] >> 16
        opcode = words[at] & 0xFFFF
        writer.need(count > 0 and at + count <= len(words), "SPIR-V: a damaged instruction")
        if opcode in ANNOTATIONS and first is None:
            first = at
        if opcode == OP_VARIABLE and count >= 4 and words[at + 3] == STORAGE_PUSH_CONSTANT:
            roots.append(words[at + 2])
        at += count
    if not roots:
        return code
    writer.need(first is not None, "SPIR-V: push constants without a Block decoration")
    added = []
    for ident in roots:
        added += [4 << 16 | writer.OP_DECORATE, ident, writer.DECORATION_SET, writer.D3D12_SPACE,
                  4 << 16 | writer.OP_DECORATE, ident, writer.DECORATION_BINDING,
                  writer.D3D12_ROOT]
    words[first:first] = added
    return struct.pack(f"<{len(words)}I", *words)


def place_heaps(writer, code):
    """The SPIR-V with each heap variable at binding 0 of its own set
    from the heap's first space."""
    words = list(struct.unpack(f"<{len(code) // 4}I", code))
    sets = {}
    order = []
    at = 5
    while at < len(words):
        count = words[at] >> 16
        opcode = words[at] & 0xFFFF
        if opcode == writer.OP_DECORATE and count >= 4 and words[at + 2] == writer.DECORATION_SET:
            sets[words[at + 1]] = at
        if opcode == OP_VARIABLE and count >= 4:
            order.append(words[at + 2])
        at += count
    heap = [v for v in order if v in sets and words[sets[v] + 3] == writer.HEAP_SET]
    at = 5
    while at < len(words):
        count = words[at] >> 16
        if ((words[at] & 0xFFFF) == writer.OP_DECORATE and count >= 4 and
                words[at + 1] in heap):
            if words[at + 2] == writer.DECORATION_SET:
                words[at + 3] = writer.D3D12_HEAP_SPACE + heap.index(words[at + 1])
            elif words[at + 2] == writer.DECORATION_BINDING:
                words[at + 3] = 0
        at += count
    return struct.pack(f"<{len(words)}I", *words)


def constant_defines(writer, constants, fixed):
    """The HLSL declaring the constants' buffer, and DXC's definitions of
    each unfixed constant as a read of it."""
    read = [(i, c) for i, c in enumerate(constants) if c["id"] not in fixed]
    if not read:
        return "", []
    rows = (len(constants) + 3) // 4
    declaration = (f"cbuffer mrhiConstants : register(b{writer.D3D12_CONSTANTS}, "
                   f"space{writer.D3D12_SPACE})\n"
                   f"{{\n    uint4 mrhiConstant[{rows}];\n}};\n")
    defines = []
    for index, constant in read:
        bits = f"mrhiConstant[{index // 4}].{'xyzw'[index % 4]}"
        defines += ["-D", f"SPIRV_CROSS_CONSTANT_ID_{constant['id']}=" +
                    READS[constant["type"]].format(bits)]
    return declaration, defines


# A statement of a continue block: not a block's start or end, and not a
# branch, a loop, a jump or a label. SPIRV-Cross puts every brace on a
# line of its own, so a line's start says what it is; hand-written HLSL
# would need a tokenizer.
PLAIN = re.compile(r"(?!(if|else|for|while|do|switch|case|default|return|break)\b)[^{}]")
# The ends of a continue block: a for loop's continue, and a do-while
# loop's condition, which reads the stepped value as the loop means it.
CONTINUED = re.compile(r"continue;$|\}\s*while\s*\(")


def stale_reads(source):
    """Each read of a loop's variable after its continue block has stepped
    it, as (line, variable), in SPIRV-Cross's HLSL. SPIRV-Cross writes a
    continue block as one statement per loop variable, and may fold a load
    into a later one's: `_5736++;` then `_5737 += ...Load(_5736 * 4)...`
    reads the next iteration's element where the SPIR-V reads this one's.
    A continue block is plain statements up to the loop's `continue;`, or
    to `} while (` where SPIRV-Cross writes a loop branching back on a
    condition as a do-while; a step with a branch, a loop or a block's end
    before that is the body's own, as the WGSL steps it, and a read after
    it wants the stepped value. SPIRV-Cross's MSL copies the variable first, so only HLSL shows
    it."""
    found = []
    lines = source.splitlines()
    for at, line in enumerate(lines):
        stepped = re.match(r"\s*(_\d+)(\+\+|--);\s*$", line)
        if not stepped:
            continue
        variable = re.compile(rf"\b{stepped.group(1)}\b")
        read = None
        for later in range(at + 1, len(lines)):
            text = lines[later].strip()
            if not text:
                continue
            if CONTINUED.match(text):
                found += [read] if read else []
                break
            if not PLAIN.match(text):
                break
            if read is None and variable.search(text):
                read = (later + 1, stepped.group(1))
    return found


def run(command, what):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed:\n{result.stdout}{result.stderr}")


def main():
    if len(sys.argv) != 4:
        print("usage: mrhi_dxil.py SPIRV REFLECTION DIR", file=sys.stderr)
        return 2
    spirv_path, reflection_path, folder = sys.argv[1:]
    writer = load_writer()
    try:
        with open(spirv_path, "rb") as f:
            code = f.read()
        with open(reflection_path, encoding="utf-8") as f:
            reflection = json.load(f)
        entries = reflection["entries"]
        placed = place_heaps(writer, place_root(writer, code))
        float16 = writer.CAPABILITY_FLOAT16 in writer.capabilities(code)
        declaration, defines = constant_defines(writer, reflection.get("constants", []),
                                                writer.fixed_constants(code))
    except (OSError, ValueError, KeyError, TypeError, writer.ContainerError) as error:
        print(f"mrhi_dxil: {error}", file=sys.stderr)
        return 1
    os.makedirs(folder, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        module = os.path.join(work, "placed.spv")
        with open(module, "wb") as f:
            f.write(placed)
        try:
            for entry in entries:
                stage, profile = STAGES[entry["stage"]]
                # SV_ViewID, the view index, needs 6.1; 16-bit floats need
                # 6.2 and dxc's 16-bit types.
                model = "6_1" if "view_index" in entry.get("builtins", []) else SHADER_MODEL
                model = "6_2" if float16 else model
                hlsl = os.path.join(work, entry["name"] + ".hlsl")
                run(["spirv-cross", module, "--hlsl", "--shader-model", model.replace("_", ""),
                     *(["--hlsl-enable-16bit-types"] if float16 else []),
                     "--hlsl-support-nonzero-basevertex-baseinstance",
                     "--hlsl-basevertex-baseinstance-binding", str(writer.D3D12_VERTEX_INFO),
                     str(writer.D3D12_SPACE), "--entry", entry["name"], "--stage", stage,
                     "--output", hlsl], f"spirv-cross on {entry['name']}")
                with open(hlsl, encoding="utf-8") as f:
                    source = f.read()
                stale = stale_reads(source)
                if stale:
                    line, variable = stale[0]
                    raise RuntimeError(f"spirv-cross on {entry['name']} reads {variable} at line {line} "
                                       "after its loop stepped it; read it inside the loop's body")
                with open(hlsl, "w", encoding="utf-8") as f:
                    f.write(declaration + source)
                run(["dxc", "-T", f"{profile}_{model}", "-E", "main", "-Qstrip_reflect",
                     "-Qstrip_debug", *(["-enable-16bit-types"] if float16 else []), *defines,
                     "-Fo",
                     os.path.join(folder, entry["name"] + ".dxil"), hlsl],
                    f"dxc on {entry['name']}")
        except RuntimeError as error:
            print(f"mrhi_dxil: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
