#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes a shader container (docs/contract/container.md) from a SPIR-V
# module, a WGSL module and a JSON reflection. It checks the reflection
# by the rules the library's reader applies, and refuses code that
# disagrees with it:
# - both modules hold exactly the reflection's entry points, with their
#   stages (and a WGSL compute entry's literal workgroup size);
# - every binding either module declares is in the reflection, and a
#   WGSL binding's kind, and a texture's dimension, match it.
# A module may leave out a binding it does not use.
#
# An entry may read the pass's heap (record mrhi-0015), listing its
# "heap_uses" ("sampled_textures", "storage_textures", "storage_buffers",
# "samplers", and "writes" with a storage kind, never in a vertex entry).
# SPIR-V reads the resource heap at set 4 binding 0 and the sampler heap
# at set 4 binding 1 (DXC: -fvk-bind-resource-heap 0 4
# -fvk-bind-sampler-heap 1 4). WGSL reads no heaps yet, so a container
# whose entries use one has no WGSL: pass - for it.
#
# The enum names are the contract's (docs/contract/mrhi.json) without
# their prefixes. The reflection:
#   {
#     "root_block_bytes": 16,
#     "entries": [
#       {"name": "vs", "stage": "vertex",
#        "inputs": [{"location": 0, "type": "float32", "components": 3}],
#        "variables": [{"location": 0, "type": "float32", "components": 2}]},
#       {"name": "fs", "stage": "fragment", "builtins": ["front_facing"],
#        "variables": [{"location": 0, "type": "float32", "components": 2,
#                       "interpolation": "perspective", "sampling": "center"}],
#        "outputs": [{"location": 0, "type": "float32", "components": 4}]},
#       {"name": "cs", "stage": "compute", "workgroup": [8, 8, 1],
#        "workgroup_storage_bytes": 1024, "heap_uses": []}
#     ],
#     "bindings": [
#       {"table": 0, "slot": 0, "kind": "uniform_buffer",
#        "stages": ["vertex"], "min_size": 64},
#       {"table": 0, "slot": 1, "kind": "sampler",
#        "stages": ["fragment"], "sampler": "filtering"},
#       {"table": 0, "slot": 2, "kind": "sampled_texture",
#        "stages": ["fragment"], "sample_type": "float",
#        "view_dimension": "2d", "multisampled": false},
#       {"table": 1, "slot": 0, "kind": "storage_texture",
#        "stages": ["compute"], "access": "write_only",
#        "format": "rgba8_unorm", "view_dimension": "2d"}
#     ],
#     "constants": [{"id": 0, "type": "float32", "default": 1.0},
#                   {"id": 1, "type": "uint32"}]
#   }
# A vertex entry's variables are its outputs and a fragment entry's its
# inputs; interpolation defaults to perspective at the center, and flat
# from the first vertex for integers. A constant without a default must
# be set by every pipeline.
#
# usage: mrhi_container.py SPIRV WGSL|- REFLECTION OUTPUT
# Standard library only; exits 1 naming the first problem.

import hashlib
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = os.path.join(ROOT, "docs", "contract", "mrhi.json")
SPIRV_MAGIC = 0x07230203
COLOR_TARGETS = 8
MAX_ROOT_BLOCK = 256
MAX_NAME = 256
MAX_RECORDS = 4096
TABLES = 4
# SPIR-V: OpEntryPoint, OpDecorate, and the decorations Binding and
# DescriptorSet; execution models by stage.
OP_ENTRY_POINT = 15
OP_DECORATE = 71
DECORATION_BINDING = 33
DECORATION_SET = 34
EXECUTION_MODELS = {0: "vertex", 4: "fragment", 5: "compute"}
# The heap's set and bindings in SPIR-V.
HEAP_SET = 4
RESOURCE_HEAP = 0
SAMPLER_HEAP = 1
RESOURCE_USES = ("sampled_textures", "storage_textures", "storage_buffers")


class ContainerError(Exception):
    pass


def enum(contract, name, prefix):
    """The contract enum's values by their names without the prefix."""
    for header in contract["headers"]:
        for item in header["items"]:
            if item.get("name") == name:
                return {v["name"][len(prefix):]: v["value"] for v in item["values"]}
    raise ContainerError(f"the contract has no enum {name}")


class Enums:
    def __init__(self, contract):
        self.stages = enum(contract, "shader_stages", "stage_")
        self.kinds = enum(contract, "binding_kind", "binding_")
        self.samplers = enum(contract, "sampler_binding", "sampler_")
        self.sample_types = enum(contract, "sample_type", "sample_")
        self.dimensions = enum(contract, "texture_kind", "texture_")
        self.accesses = enum(contract, "storage_access", "storage_")
        self.formats = enum(contract, "format", "format_")
        self.scalars = enum(contract, "scalar_type", "scalar_")
        self.interpolations = enum(contract, "interpolation", "interpolation_")
        self.samplings = enum(contract, "sampling", "sampling_")
        self.builtins = enum(contract, "shader_builtins", "builtin_")
        self.heap_uses = enum(contract, "shader_heap_uses", "heap_use_")
        self.constants = enum(contract, "constant_type", "constant_")


def need(condition, message):
    if not condition:
        raise ContainerError(message)


def pick(table, value, what, allow_none=False):
    need(isinstance(value, str) and value in table and (allow_none or value != "none"),
         f"unknown {what}: {value!r}")
    return table[value]


def number(value, what, low=0, high=0xFFFFFFFF):
    need(isinstance(value, int) and not isinstance(value, bool) and low <= value <= high,
         f"{what} must be an integer from {low} to {high}: {value!r}")
    return value


def keys(item, allowed, where):
    need(isinstance(item, dict), f"{where} is a JSON object")
    unknown = sorted(set(item) - set(allowed))
    need(not unknown, f"{where}: unknown keys {unknown}")


def unique(values, what):
    need(len(set(values)) == len(values), f"repeated {what}")


# Reflection records, packed as the container lays them out.

def pack_variable(item, role, enums, where):
    """An interface record: a vertex input, a color output or an
    inter-stage variable, whose interpolation defaults as WGSL's does."""
    keys(item, ("location", "type", "components", "interpolation", "sampling"), where)
    high = COLOR_TARGETS - 1 if role == "output" else 0xFFFFFFFF
    location = number(item.get("location"), f"{where}: a location", 0, high)
    kind = item.get("type")
    code = pick(enums.scalars, kind, "scalar type")
    need(role != "input" or kind != "float16", f"{where}: a vertex input is not a 16-bit float")
    components = number(item.get("components"), f"{where}: the components", 1, 4)
    interpolation = sampling = 0
    if role == "variable":
        integer = kind in ("sint32", "uint32")
        mode = item.get("interpolation", "flat" if integer else "perspective")
        sample = item.get("sampling", "first" if mode == "flat" else "center")
        need(not integer or mode == "flat", f"{where}: integers interpolate flat")
        allowed = ("first", "either") if mode == "flat" else ("center", "centroid", "sample")
        need(sample in allowed, f"{where}: {mode} interpolation samples at {allowed}")
        interpolation = pick(enums.interpolations, mode, "interpolation")
        sampling = pick(enums.samplings, sample, "sampling")
    else:
        need("interpolation" not in item and "sampling" not in item,
             f"{where}: only inter-stage variables interpolate")
    return location, struct.pack("<IBBBB", location, code, components, interpolation, sampling)


def pack_range(entry, key, role, enums, records, where, allowed):
    """Packs an entry's interface records into records; their range."""
    items = entry.get(key, [])
    need(isinstance(items, list), f"{where}: {key} is a list")
    need(not items or allowed, f"{where}: this stage has no {key}")
    first = len(records)
    for item in items:
        records.append(pack_variable(item, role, enums, where))
    unique([loc for loc, _ in records[first:]], f"{key} location in {where}")
    need(len(records) <= MAX_RECORDS, "too many records")
    return first, len(items)


def pack_entries(reflection, enums, strings, inputs, outputs, variables):
    entries = reflection.get("entries")
    need(isinstance(entries, list) and entries, "the reflection needs entries")
    unique([e.get("name") for e in entries], "entry name")
    records = []
    for entry in entries:
        keys(entry, ("name", "stage", "workgroup", "workgroup_storage_bytes", "builtins",
                     "inputs", "outputs", "variables", "heap_uses"), "an entry")
        name = entry.get("name")
        need(isinstance(name, str), "an entry needs a name")
        encoded = name.encode("utf-8")
        need(0 < len(encoded) <= MAX_NAME and b"\0" not in encoded,
             f"entry name {name!r} must be 1 to {MAX_NAME} bytes without NUL")
        where = f"entry {name}"
        stage = entry.get("stage")
        need(stage in ("vertex", "fragment", "compute"), f"{where}: unknown stage {stage!r}")
        compute = stage == "compute"
        workgroup = entry.get("workgroup", [0, 0, 0])
        need(isinstance(workgroup, list) and len(workgroup) == 3,
             f"{where}: the workgroup is three sizes")
        low, high = (1, 0xFFFFFFFF) if compute else (0, 0)
        workgroup = [number(w, f"{where}: a workgroup size", low, high) for w in workgroup]
        storage = number(entry.get("workgroup_storage_bytes", 0), f"{where}: workgroup storage",
                         0, 0xFFFFFFFF if compute else 0)
        builtins = entry.get("builtins", [])
        need(isinstance(builtins, list) and (not builtins or stage == "fragment"),
             f"{where}: only fragment entries list builtins")
        unique(builtins, f"builtin in {where}")
        mask = 0
        for builtin in builtins:
            mask |= pick(enums.builtins, builtin, "builtin")
        uses = entry.get("heap_uses", [])
        need(isinstance(uses, list), f"{where}: the heap uses are a list")
        unique(uses, f"heap use in {where}")
        heap = 0
        for use in uses:
            heap |= pick(enums.heap_uses, use, "heap use")
        stored = {"storage_textures", "storage_buffers"} & set(uses)
        need("writes" not in uses or (stored and stage != "vertex"),
             f"{where}: heap writes need a storage kind, outside a vertex entry")
        ranges = [
            pack_range(entry, "inputs", "input", enums, inputs, where, stage == "vertex"),
            pack_range(entry, "outputs", "output", enums, outputs, where, stage == "fragment"),
            pack_range(entry, "variables", "variable", enums, variables, where, not compute),
        ]
        records.append(struct.pack("<III3I6HIII", enums.stages[stage], len(strings),
                                   len(encoded), *workgroup,
                                   *[n for pair in ranges for n in pair], mask, storage, heap))
        strings += encoded
    return records


def pack_binding(binding, enums):
    where = f"binding {binding.get('table')}.{binding.get('slot')}"
    keys(binding, ("table", "slot", "kind", "stages", "sampler", "sample_type", "access",
                   "format", "view_dimension", "multisampled", "min_size"), where)
    table = number(binding.get("table"), f"{where}: the table", 0, TABLES - 1)
    slot = number(binding.get("slot"), f"{where}: the slot", 0, 0xFFFF)
    kind = binding.get("kind")
    pick(enums.kinds, kind, "binding kind")
    stages = binding.get("stages")
    need(isinstance(stages, list) and stages, f"{where}: it needs stages")
    unique(stages, f"stage in {where}")
    mask = 0
    for stage in stages:
        mask |= pick(enums.stages, stage, "stage")
    buffer = kind.endswith("_buffer")
    sampler = pick(enums.samplers, binding.get("sampler", "none"), "sampler binding", True)
    sample = pick(enums.sample_types, binding.get("sample_type", "none"), "sample type", True)
    access = pick(enums.accesses, binding.get("access", "none"), "storage access", True)
    fmt = pick(enums.formats, binding.get("format", "none"), "format", True)
    texture = kind in ("sampled_texture", "storage_texture")
    dimension = binding.get("view_dimension")
    need((dimension is None) != texture, f"{where}: only textures have a view dimension")
    dimension = pick(enums.dimensions, dimension, "view dimension") if texture else 0
    multisampled = binding.get("multisampled", False)
    need(isinstance(multisampled, bool), f"{where}: multisampled is true or false")
    min_size = number(binding.get("min_size", 0), f"{where}: the minimum size", 0, 2**64 - 1)
    need((sampler != 0) == (kind == "sampler"), f"{where}: a sampler, and only one, has a type")
    need((sample != 0) == (kind == "sampled_texture"),
         f"{where}: a sampled texture, and only one, has a sample type")
    need((access != 0) == (kind == "storage_texture") and (fmt != 0) == (access != 0),
         f"{where}: a storage texture, and only one, has an access and a format")
    need(kind != "storage_texture" or binding.get("view_dimension") in ("2d", "2d_array", "3d"),
         f"{where}: a storage texture is 2D, a 2D array or 3D")
    need(not multisampled or (kind == "sampled_texture" and binding.get("view_dimension") == "2d"
                              and binding.get("sample_type") != "float"),
         f"{where}: a multisampled texture is 2D and not filterable")
    need(buffer or min_size == 0, f"{where}: only buffers have a minimum size")
    writable = kind == "storage_buffer" or (kind == "storage_texture"
                                            and binding.get("access") != "read_only")
    need(not (writable and "vertex" in stages), f"{where}: the vertex stage cannot write it")
    record = struct.pack("<BBHIBBBBHBxQ", table, enums.kinds[kind], slot, mask, sampler, sample,
                         dimension, access, fmt, int(multisampled), min_size)
    return (table, slot), record


def pack_constant(constant, enums):
    keys(constant, ("id", "type", "default"), "a constant")
    ident = number(constant.get("id"), "a constant's id")
    kind = constant.get("type")
    code = pick(enums.constants, kind, "constant type")
    required = "default" not in constant
    default = constant.get("default", False if kind == "bool" else 0)
    if kind == "bool":
        need(isinstance(default, bool), f"constant {ident}: the default is true or false")
        bits = int(default)
    elif kind == "float32":
        need(isinstance(default, (int, float)) and not isinstance(default, bool),
             f"constant {ident}: the default is a number")
        bits = struct.unpack("<I", struct.pack("<f", float(default)))[0]
    else:
        low, high = (-2**31, 2**31 - 1) if kind == "int32" else (0, 2**32 - 1)
        bits = number(default, f"constant {ident}: the default", low, high) & 0xFFFFFFFF
    return ident, struct.pack("<IB3xIB3x", ident, code, bits, int(required))


# The code's entry points and bindings.

def strip_wgsl_comments(text):
    text = re.sub(r"//[^\n]*", "", text)
    out = []
    depth = 0
    i = 0
    while i < len(text):
        if text.startswith("/*", i):
            depth += 1
            i += 2
        elif depth and text.startswith("*/", i):
            depth -= 1
            i += 2
        else:
            if not depth:
                out.append(text[i])
            i += 1
    return "".join(out)


ATTRIBUTES = r"((?:@\w+\s*(?:\([^)]*\))?\s*)+)"
WGSL_ENTRY = re.compile(ATTRIBUTES + r"fn\s+(\w+)")
WGSL_VAR = re.compile(ATTRIBUTES + r"var\s*(<[^>]*>)?\s*\w+\s*:\s*(\w+)")


def attribute(attributes, name):
    match = re.search(r"@" + name + r"\s*\(([^)]*)\)", attributes)
    return match.group(1).strip() if match else None


def wgsl_facts(space, type_name):
    """What a WGSL declaration says of its binding, in the reflection's
    terms: its kind, and the details the declaration fixes."""
    space = (space or "").strip("<> ").replace(" ", "")
    if space == "uniform":
        return {"kind": "uniform_buffer"}
    if space.startswith("storage"):
        writable = space.endswith("read_write")
        return {"kind": "storage_buffer" if writable else "read_only_storage_buffer"}
    if type_name in ("sampler", "sampler_comparison"):
        return {"kind": "sampler", "comparison": type_name == "sampler_comparison"}
    match = re.fullmatch(r"texture_(storage_|depth_|multisampled_|depth_multisampled_)?"
                         r"(2d_array|2d|cube_array|cube|3d)", type_name)
    need(match is not None, f"WGSL: unsupported binding type {type_name}")
    variant = match.group(1) or ""
    if variant == "storage_":
        return {"kind": "storage_texture", "view_dimension": match.group(2)}
    return {"kind": "sampled_texture", "view_dimension": match.group(2),
            "depth": variant.startswith("depth_"),
            "multisampled": variant.endswith("multisampled_")}


def check_wgsl(text, reflection, bindings):
    text = strip_wgsl_comments(text)
    entries = {}
    for match in WGSL_ENTRY.finditer(text):
        attributes, name = match.groups()
        stages = [s for s in ("vertex", "fragment", "compute") if re.search(r"@" + s + r"\b",
                                                                            attributes)]
        if stages:
            entries[name] = (stages[0], attribute(attributes, "workgroup_size"))
    wanted = {e["name"]: e for e in reflection["entries"]}
    need(set(entries) == set(wanted),
         f"WGSL entry points {sorted(entries)} differ from the reflection's {sorted(wanted)}")
    for name, (stage, size) in entries.items():
        need(stage == wanted[name]["stage"], f"WGSL: entry {name} is a {stage} entry")
        literal = size is not None and re.fullmatch(r"\d+[iu]?(\s*,\s*\d+[iu]?){0,2}\s*,?", size)
        # The reflection's size is fixed, so no override may change it.
        need(stage != "compute" or literal, f"WGSL: entry {name}'s workgroup size is not literal")
        if stage == "compute":
            sizes = [int(s) for s in re.findall(r"\d+", size)] + [1, 1]
            need(sizes[:3] == wanted[name]["workgroup"], f"WGSL: entry {name}'s workgroup size")
    for match in WGSL_VAR.finditer(text):
        attributes, space, type_name = match.groups()
        group = attribute(attributes, "group")
        slot = attribute(attributes, "binding")
        if group is None and slot is None:
            continue
        need(group is not None and slot is not None and group.isdigit() and slot.isdigit(),
             "WGSL: a binding needs literal @group and @binding")
        key = (int(group), int(slot))
        need(key in bindings, f"WGSL binds {key[0]}.{key[1]}, which the reflection does not")
        facts = wgsl_facts(space, type_name)
        declared = bindings[key]
        where = f"WGSL: binding {key[0]}.{key[1]}"
        need(facts["kind"] == declared["kind"], f"{where} is a {facts['kind']}")
        need(facts.get("view_dimension") in (None, declared.get("view_dimension")),
             f"{where} is a {facts.get('view_dimension')} texture")
        need(facts.get("comparison") in (None, declared.get("sampler") == "comparison"),
             f"{where}: sampler or sampler_comparison differs from the reflection")
        need(facts.get("depth") in (None, declared.get("sample_type") == "depth"),
             f"{where}: a depth texture has the depth sample type, and only one")
        need(facts.get("multisampled") in (None, declared.get("multisampled", False)),
             f"{where}: multisampling differs")


def check_spirv(code, reflection, bindings):
    need(len(code) >= 20 and len(code) % 4 == 0, "SPIR-V: not a whole module")
    words = struct.unpack(f"<{len(code) // 4}I", code)
    need(words[0] == SPIRV_MAGIC, "SPIR-V: not little-endian SPIR-V")
    entries = {}
    sets = {}
    slots = {}
    at = 5
    while at < len(words):
        count = words[at] >> 16
        opcode = words[at] & 0xFFFF
        need(count > 0 and at + count <= len(words), "SPIR-V: a damaged instruction")
        operands = words[at + 1:at + count]
        if opcode == OP_ENTRY_POINT and len(operands) >= 3:
            raw = struct.pack(f"<{len(operands) - 2}I", *operands[2:])
            name = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
            need(operands[0] in EXECUTION_MODELS, f"SPIR-V: entry {name} has an unknown model")
            entries[name] = EXECUTION_MODELS[operands[0]]
        elif opcode == OP_DECORATE and len(operands) >= 3:
            if operands[1] == DECORATION_SET:
                sets[operands[0]] = operands[2]
            elif operands[1] == DECORATION_BINDING:
                slots[operands[0]] = operands[2]
        at += count
    wanted = {e["name"]: e["stage"] for e in reflection["entries"]}
    need(entries == wanted, f"SPIR-V entry points {entries} differ from the reflection's {wanted}")
    uses = {use for e in reflection["entries"] for use in e.get("heap_uses", [])}
    heaps = set()
    if uses & set(RESOURCE_USES):
        heaps.add((HEAP_SET, RESOURCE_HEAP))
    if "samplers" in uses:
        heaps.add((HEAP_SET, SAMPLER_HEAP))
    for target, slot in slots.items():
        key = (sets.get(target, 0), slot)
        need(key in bindings or key in heaps,
             f"SPIR-V binds {key[0]}.{key[1]}, which the reflection does not")


# The container.

def pad8(data):
    return data + bytes(-len(data) % 8)


def build(spirv, wgsl, reflection, enums):
    keys(reflection, ("root_block_bytes", "entries", "bindings", "constants"), "the reflection")
    root = number(reflection.get("root_block_bytes", 0), "the root block's bytes", 0,
                  MAX_ROOT_BLOCK)
    need(root % 4 == 0, "the root block's bytes are a multiple of 4")
    strings = bytearray()
    inputs = []
    outputs = []
    variables = []
    entries = pack_entries(reflection, enums, strings, inputs, outputs, variables)
    bindings = {}
    binding_records = []
    for binding in reflection.get("bindings", []):
        key, record = pack_binding(binding, enums)
        need(key not in bindings, f"binding {key[0]}.{key[1]} is repeated")
        bindings[key] = binding
        binding_records.append(record)
    constants = [pack_constant(c, enums) for c in reflection.get("constants", [])]
    unique([ident for ident, _ in constants], "constant id")
    need(len(binding_records) <= MAX_RECORDS and len(constants) <= MAX_RECORDS,
         "too many records")
    check_spirv(spirv, reflection, bindings)
    heaps = any(e.get("heap_uses") for e in reflection["entries"])
    if heaps:
        need(wgsl is None, "WGSL reads no heaps: a container using one has no WGSL")
        wgsl = b""
    else:
        need(wgsl is not None, "a container using no heap needs WGSL")
        try:
            text = wgsl.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ContainerError(f"WGSL: not UTF-8 ({error})") from error
        need(wgsl and "\0" not in text, "WGSL: empty, or holds NUL")
        check_wgsl(text, reflection, bindings)
    sections = [
        (1, struct.pack("<I12x", root)),
        (2, bytes(strings)),
        (3, b"".join(entries)),
        (4, b"".join(binding_records)),
        (5, b"".join(r for _, r in inputs)),
        (6, b"".join(r for _, r in outputs)),
        (7, b"".join(r for _, r in constants)),
        (8, spirv),
        (9, wgsl),
        (10, b"".join(r for _, r in variables)),
    ]
    sections = [(kind, data) for kind, data in sections if kind != 9 or data]
    offset = 64 + 24 * len(sections)
    table = b""
    body = b""
    for kind, data in sections:
        table += struct.pack("<IIQQ", kind, 0, offset + len(body), len(data))
        body += pad8(data)
    size = offset + len(body)
    signed = struct.pack("<I12x", len(sections)) + table + body
    digest = hashlib.sha256(signed).digest()
    return b"MRSC" + struct.pack("<IQ", 1, size) + digest + signed


def main():
    if len(sys.argv) != 5:
        print("usage: mrhi_container.py SPIRV WGSL|- REFLECTION OUTPUT", file=sys.stderr)
        return 2
    spirv_path, wgsl_path, reflection_path, output_path = sys.argv[1:]
    try:
        with open(CONTRACT, encoding="utf-8") as f:
            enums = Enums(json.load(f))
        with open(spirv_path, "rb") as f:
            spirv = f.read()
        wgsl = None
        if wgsl_path != "-":
            with open(wgsl_path, "rb") as f:
                wgsl = f.read()
        with open(reflection_path, encoding="utf-8") as f:
            reflection = json.load(f)
        container = build(spirv, wgsl, reflection, enums)
    except (OSError, ValueError, ContainerError) as error:
        print(f"mrhi_container: {error}", file=sys.stderr)
        return 1
    with open(output_path, "wb") as f:
        f.write(container)
    return 0


if __name__ == "__main__":
    sys.exit(main())
