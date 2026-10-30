#!/usr/bin/env python3
# Builds the engine's shader containers offline, as a cook would: no
# shipped build compiles a shader. Each container's source is
# NAME.slang, Slang being the one first-party GPU language:
# slangc compiles every entry into one SPIR-V module and into WGSL,
# from the one source; the pinned compiler's version is checked, and
# RAWFRAME_SLANGC names it where it is not on the path. spirv-val checks
# the module for Vulkan 1.3, and Maul RHI's container writer
# (third_party/maul-rhi/tools/mrhi_container.py) writes the containers a
# build's driver reads, each with the SPIR-V and each entry in WGSL,
# which the writer asks of every container: NAME.mrsc, for Vulkan and
# WebGPU; NAME.metal.mrsc, with each entry in Metal's language too, crossed
# from the SPIR-V by Maul RHI's mrhi_msl.py through SPIRV-Cross; and
# NAME.d3d12.mrsc, with each entry's DXIL, crossed to HLSL and compiled by
# DXC through its mrhi_dxil.py. They land in the module's
# src/generated, and the build embeds the one its driver reads
# (rawframe_shader_containers in cmake/rawframe.cmake). The check runs none
# of these tools; the containers are committed, and this is run again
# whenever a source changes. spirv-cross and dxc must be on the path.
#
# Each directory of containers has a stamp, containers.stamp: the Maul RHI
# release whose tools cooked them, the flags its DXC compiles passed, and
# each container's SHA-256. The container format has no place for them,
# and DXIL compiled without -Gis cannot be told apart by its bytes. DXC
# without -Gis marks every float operation fast and folds the math, so
# D3D12 would compute other than the source and the other APIs; this
# refuses to cook, a game's material too, when Maul RHI's mrhi_dxil.py
# passes no -Gis. `--check`, which the check runs and which needs no
# tool, refuses a stamp without -Gis or with other flags than the
# vendored mrhi_dxil.py passes, one of another Maul RHI release than the
# vendored one, and a container that is missing, unstamped or not the
# bytes stamped. It also refuses a container in a module's generated
# directory that no source gives, and a name a module's CMake files
# embed that this does not make. A move of Maul RHI therefore cooks the
# containers again.
#
# With `--material`, the three containers of each container linked with a
# material, the scene's and the shadow's, are built linked with the
# material module given in place of the engine's own, and written to the
# directory given, as the cook builds them for a game's material.
#
#
# A toolchain laid out as Studio's package lays one has the programs
# in a `bin` beside `tools`: they are taken first, its Slang compiler before
# the one RAWFRAME_SLANGC names, as the cook takes them.
#
# usage: tools/gen_shaders.py
#        tools/gen_shaders.py --material <material.slang> <directory>
#        tools/gen_shaders.py --check [root]

import ast
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
if os.path.isdir(BIN):
    os.environ["PATH"] = BIN + os.pathsep + os.environ.get("PATH", "")
WRITER = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_container.py")
METAL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_msl.py")
DXIL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_dxil.py")
# The Slang compiler every container is built with: its exact release, as
# the language is defined by the pinned compiler until it is ratified.
SLANGC = os.environ.get("RAWFRAME_SLANGC", "slangc")
for packaged in (os.path.join(BIN, "slangc"), os.path.join(BIN, "slangc.exe")):
    if os.path.isfile(packaged):
        SLANGC = packaged
SLANG_VERSION = "2026.19"
# The containers whose vertex position must be invariant.
INVARIANT = ("scene",)
# Containers whose shading reads its surface through a material:
# the scene declares its material extern and is linked with
# the module that exports it, the blob's for the containers the engine
# ships, beside the module of what a material is, and the scene's also with
# its lighting model.
LINKED = {"scene": ("blob_material", "material", "lighting"), "shadow": ("blob_material", "material")}
# Each linked container's vertex entry and the fragment entries drawn with
# it, whose inputs D3D12 asks be exactly its outputs: a material may leave
# an input unread, which Slang drops from the entry.
DRAWN_WITH = {"scene": ("vs", ("fs", "fsDecaled", "cut", "normal", "cutNormal")), "shadow": ("vsCut", ("cut",))}
# Materials written for a module's tests: its module and its name,
# built from tests/materials/NAME.slang with each linked container, as
# `--material` builds a game's, into tests/generated/NAME.CONTAINER.
TEST_MATERIALS = (("render_scene_gpu", "checker"), ("render_scene_gpu", "faded"), ("render_scene_gpu", "faded_graph"),
                  ("render_scene_gpu", "rising_graph"), ("render_scene_gpu", "facing_graph"),
                  ("render_scene_gpu", "tinted_graph"), ("render_scene_gpu", "spaced_graph"),
                  ("render_scene_gpu", "coated_graph"), ("render_scene_gpu", "brushed_graph"),
                  ("render_scene_gpu", "glass_graph"), ("render_scene_gpu", "frosted_graph"))
# Each container: its module and its name.
CONTAINERS = (
    ("render", "display"),
    ("render_canvas_gpu", "sprite"),
    ("render_canvas_gpu", "ui"),
    ("render_scene_gpu", "scene"),
    ("render_scene_gpu", "occlusion"),
    ("render_scene_gpu", "reflect"),
    ("render_scene_gpu", "motion"),
    ("render_scene_gpu", "focus"),
    ("render_scene_gpu", "contact"),
    ("render_scene_gpu", "decal"),
    ("render_scene_gpu", "probe"),
    ("render_scene_gpu", "resolve"),
    ("render_scene_gpu", "fxaa"),
    ("render_scene_gpu", "bloom"),
    ("render_scene_gpu", "meter"),
    ("render_scene_gpu", "skin"),
    ("render_scene_gpu", "shadow"),
    ("render_scene_gpu", "sky"),
    ("render_scene_gpu", "temporal"),
    ("render_scene_gpu", "tonemap"),
    ("render_scene_gpu", "post"),
    ("particles_gpu", "spawn"),
    ("particles_gpu", "particle"),
)


# The stamp beside each directory of containers, and the containers a
# source gives: SPIR-V and WGSL, with Metal, with DXIL.
STAMP = "containers.stamp"
SUFFIXES = ("", ".metal", ".d3d12")


def dxcFlags(root):
    """The DXC_FLAGS the vendored mrhi_dxil.py assigns, read from its
    source without running it; None when it assigns no tuple of strings."""
    script = os.path.join(root, "third_party", "maul-rhi", "tools", "mrhi_dxil.py")
    with open(script, encoding="utf-8") as file:
        tree = ast.parse(file.read())
    for node in tree.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and
                node.targets[0].id == "DXC_FLAGS"):
            try:
                flags = ast.literal_eval(node.value)
            except ValueError:
                return None
            if isinstance(flags, tuple) and all(isinstance(flag, str) for flag in flags):
                return flags
            return None
    return None


def rhiRelease(root):
    """The vendored Maul RHI's release, from its version macros."""
    with open(os.path.join(root, "third_party", "maul-rhi", "include", "maul-rhi", "base.h"), encoding="utf-8") as file:
        text = file.read()
    parts = [re.search(rf"^#define MRHI_VERSION_{part} (\d+)$", text, re.M) for part in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(part.group(1) for part in parts) if all(parts) else None


def strictFlags():
    """The vendored DXC flags, when they keep the float math strict."""
    flags = dxcFlags(ROOT)
    if flags is None or "-Gis" not in flags:
        sys.exit(f"{DXIL} compiles DXIL without -Gis: D3D12 would fold the float math the other APIs keep")
    return flags


def containers(root):
    """Each directory of containers and the names of those it holds."""
    held = {}
    for module, name in TEST_MATERIALS:
        directory = os.path.join(root, "modules", module, "tests", "generated")
        held.setdefault(directory, []).extend(f"{name}.{linked}{suffix}.mrsc" for linked in LINKED
                                              for suffix in SUFFIXES)
    for module, name in CONTAINERS:
        directory = os.path.join(root, "modules", module, "src", "generated")
        held.setdefault(directory, []).extend(f"{name}{suffix}.mrsc" for suffix in SUFFIXES)
    return held


def digest(path):
    with open(path, "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()


def stamp(directory, names, flags):
    lines = [
        "# Written by tools/gen_shaders.py and read by its --check: the Maul RHI",
        "# release whose tools cooked these containers, the flags its DXC",
        "# compiles passed, and each container's SHA-256.",
        f"maul-rhi {rhiRelease(ROOT)}",
        f"dxc-flags {' '.join(flags)}",
    ]
    lines += [f"container {name} {digest(os.path.join(directory, name))}" for name in sorted(names)]
    with open(os.path.join(directory, STAMP), "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines) + "\n")


def embeddedNames(root):
    """Each (listing, module, name) a rawframe_shader_containers call names,
    in every CMakeLists.txt and .cmake file under modules/: the command in
    any case, with space before its parenthesis, and NAMES wherever it
    stands among the arguments, as CMake reads it."""
    listings = glob.glob(os.path.join(root, "modules", "**", "CMakeLists.txt"), recursive=True)
    listings += glob.glob(os.path.join(root, "modules", "**", "*.cmake"), recursive=True)
    for listing in sorted(listings):
        module = os.path.relpath(listing, os.path.join(root, "modules")).split(os.sep)[0]
        with open(listing, encoding="utf-8") as file:
            text = re.sub(r"#.*", "", file.read())
        for call in re.finditer(r"\brawframe_shader_containers\s*\(([^)]*)\)", text, re.I):
            words = call.group(1).split()
            at = 0
            while at < len(words):
                if words[at] == "NAMES":
                    at += 1
                    while at < len(words) and words[at] != "TARGET":
                        yield listing, module, words[at]
                        at += 1
                else:
                    at += 2 if words[at] == "TARGET" else 1


def problems(root):
    """What in root's containers no longer matches their stamps; empty
    when every one is current."""
    found = []
    flags = dxcFlags(root)
    release = rhiRelease(root)
    for directory, names in containers(root).items():
        path = os.path.join(directory, STAMP)
        shown = os.path.relpath(path, root)
        if not os.path.isfile(path):
            found.append(f"{shown}: missing; run tools/gen_shaders.py")
            continue
        with open(path, encoding="utf-8") as file:
            text = file.read()
        stamped = re.search(r"^maul-rhi (\S+)$", text, re.M)
        if stamped is None or stamped.group(1) != release:
            found.append(f"{shown}: cooked with Maul RHI {stamped.group(1) if stamped else 'of no release'}, "
                         f"the vendored one is {release}")
        cooked = re.search(r"^dxc-flags (.*)$", text, re.M)
        if cooked is None or "-Gis" not in cooked.group(1).split():
            found.append(f"{shown}: its DXIL was compiled without -Gis")
        elif flags is None or cooked.group(1) != " ".join(flags):
            found.append(f"{shown}: its DXIL was compiled with {cooked.group(1)}, the vendored mrhi_dxil.py "
                         f"passes {' '.join(flags) if flags else 'no flags it states'}")
        hashes = dict(re.findall(r"^container (\S+) ([0-9a-f]{64})$", text, re.M))
        for name in names:
            held = os.path.join(directory, name)
            if not os.path.isfile(held):
                found.append(f"{os.path.relpath(held, root)}: missing")
            elif name not in hashes:
                found.append(f"{os.path.relpath(held, root)}: not in {STAMP}")
            elif hashes[name] != digest(held):
                found.append(f"{os.path.relpath(held, root)}: not the bytes {STAMP} names")
        for name in sorted(set(hashes) - set(names)):
            found.append(f"{shown}: names {name}, which no source gives")
    # A container on disk that no source gives is stamped by nothing; the
    # build would embed it all the same if CMake named it.
    held = containers(root)
    for path in sorted(glob.glob(os.path.join(root, "modules", "*", "*", "generated", "*.mrsc"))):
        if os.path.basename(path) not in held.get(os.path.dirname(path), ()):
            found.append(f"{os.path.relpath(path, root)}: no source gives it, so no stamp vouches for it")
    # What each module's build embeds (rawframe_shader_containers in
    # cmake/rawframe.cmake) is a container this makes.
    made = set(CONTAINERS)
    for listing, module, name in embeddedNames(root):
        if (module, name) not in made:
            found.append(f"{os.path.relpath(listing, root)}: embeds {name}, which tools/gen_shaders.py does not make")
    return found


def check(root):
    found = problems(root)
    for problem in found:
        print(f"gen_shaders --check: {problem}")
    return 1 if found else 0


def run(*command, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def signature(dxil, which):
    # A DXIL entry's input or output signature as dxc lists it: each
    # element's name, index, and register.
    listed = subprocess.run(["dxc", "-dumpbin", dxil], capture_output=True, text=True)
    if listed.returncode != 0:
        sys.exit(f"dxc cannot list {dxil}:\n{listed.stderr}")
    heading = f"; {which} signature:"
    if heading not in listed.stdout:
        return []
    rows = []
    for line in listed.stdout.split(heading, 1)[1].splitlines():
        parts = line[1:].split()
        if len(parts) >= 4 and parts[1].isdigit() and parts[3].isdigit():
            rows.append((parts[0], parts[1], parts[3]))
        elif rows and not parts:
            break
    return rows


# What a pixel shader reads from the rasterizer, not from the vertex shader.
RASTERIZED = ("SV_IsFrontFace", "SV_PrimitiveID", "SV_SampleIndex")


def checkDrawnWith(dxil, name):
    # D3D12 refuses a pipeline whose pixel shader's inputs are not the
    # first of its vertex shader's outputs, element for element, where
    # Vulkan, Metal, and WebGPU draw it; those it leaves off the end it
    # need not read.
    # The values the rasterizer makes (the face drawn) are no vertex
    # shader's to give, and are left out of the comparison.
    vertex, fragments = DRAWN_WITH[name]
    outputs = signature(os.path.join(dxil, f"{vertex}.dxil"), "Output")
    for fragment in fragments:
        inputs = [row for row in signature(os.path.join(dxil, f"{fragment}.dxil"), "Input")
                  if row[0] not in RASTERIZED]
        if inputs != outputs[: len(inputs)]:
            sys.exit(f"{name}: {fragment} reads {inputs}, where {vertex} gives {outputs}: D3D12 refuses the pair")


def invariantPosition(spirv, wgsl):
    # Slang 2026.19 cannot say a vertex position is invariant, which the
    # scene's depth prepass and lit pass need to meet exactly: the
    # SPIR-V's position is decorated Invariant, and the WGSL's vertex
    # output marked @invariant, after Slang compiles them.
    listed = subprocess.run(["spirv-dis", "--raw-id", spirv], capture_output=True, text=True)
    if listed.returncode != 0:
        sys.exit(f"spirv-dis failed:\n{listed.stderr}")
    lines = []
    for line in listed.stdout.split("\n"):
        lines.append(line)
        position = re.match(r"\s*OpDecorate (%\d+) BuiltIn Position$", line)
        if position:
            lines.append(f"OpDecorate {position.group(1)} Invariant")
    assembly = spirv + ".spvasm"
    with open(assembly, "w") as file:
        file.write("\n".join(lines))
    run("spirv-as", "--preserve-numeric-ids", "--target-env", "vulkan1.3", assembly, "-o", spirv)
    with open(wgsl) as file:
        source = file.read()
    source, marked = re.subn(r"^(\s*)@builtin\(position\)", r"\1@invariant @builtin(position)", source, flags=re.M)
    if marked == 0:
        sys.exit(f"{wgsl}: no vertex position to mark invariant")
    with open(wgsl, "w") as file:
        file.write(source)


def withoutSource(spirv):
    # Slang names its own source language (11) in OpSource, which SPIR-V
    # parsers older than Slang's language refuse: the Android emulator's
    # host side refused it and fell over. Which language a module
    # came from is debug information no driver needs, so OpSource,
    # OpSourceContinued, and OpSourceExtension are dropped, word for word
    # and nothing else moved.
    with open(spirv, "rb") as file:
        data = file.read()
    words = [int.from_bytes(data[at : at + 4], "little") for at in range(0, len(data), 4)]
    kept = words[:5]
    at = 5
    while at < len(words):
        count = words[at] >> 16
        if count == 0:
            sys.exit(f"{spirv}: an instruction of no words at {at}")
        if words[at] & 0xFFFF not in (2, 3, 4):
            kept.extend(words[at : at + count])
        at += count
    with open(spirv, "wb") as file:
        file.write(b"".join(word.to_bytes(4, "little") for word in kept))


def checkSlang():
    result = subprocess.run([SLANGC, "-version"], capture_output=True, text=True)
    version = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or version != SLANG_VERSION:
        sys.exit(f"{SLANGC} is {version or 'missing'}; the containers are built with Slang {SLANG_VERSION}")


def build(work, shaders, name, material=None):
    linked = os.path.join(work, f"{name}.spv")
    source = os.path.join(shaders, f"{name}.slang")
    # Entries of one container may read one slot as different records (a
    # particle's emitter, a ribbon's range): a pipeline holds only its own
    # entries, so Slang's overlap warning (39001) is no fault. A source
    # imports the modules beside it (screen.slang).
    quiet = ("-warnings-disable", "39001", "-I", shaders)
    inputs = (source,)
    if name in LINKED:
        # Each module compiled alone, then linked: two sources on one
        # command line would be one module, where the extern and the
        # export collide. The entries follow the module that holds them,
        # and the link runs beside the modules, where alone Slang finds
        # the ones they import.
        # A material given in place of the first linked module, the
        # engine's own: a game's, generated or written by hand.
        sources = [os.path.join(shaders, f"{module}.slang") for module in (name, *LINKED[name])]
        if material is not None:
            sources[1] = material
        modules = []
        for each in sources:
            compiled = os.path.join(work, os.path.splitext(os.path.basename(each))[0] + ".slang-module")
            run(SLANGC, each, *quiet, "-o", compiled)
            modules.append(os.path.basename(compiled))
        with open(os.path.join(shaders, f"{name}.json"), encoding="utf-8") as told:
            entries = json.load(told)["entries"]
        named = [part for entry in entries for part in ("-entry", entry["name"], "-stage", entry["stage"])]
        inputs = (modules[0], *named, *modules[1:])
    run(SLANGC, *inputs, *quiet, "-target", "spirv", "-profile", "spirv_1_6", "-fvk-use-entrypoint-name", "-o", linked,
        cwd=work)
    wgsl = os.path.join(work, f"{name}.wgsl")
    run(SLANGC, *inputs, *quiet, "-target", "wgsl", "-o", wgsl, cwd=work)
    if name in INVARIANT:
        invariantPosition(linked, wgsl)
    withoutSource(linked)
    run("spirv-val", "--target-env", "vulkan1.3", linked)
    reflection = os.path.join(shaders, f"{name}.json")
    metal = os.path.join(work, f"{name}_metal")
    # -B, here and below: Maul RHI's tools import its container writer, and
    # no bytecode cache is written into the vendored tree.
    run(sys.executable, "-B", METAL, linked, reflection, metal)
    dxil = os.path.join(work, f"{name}_dxil")
    run(sys.executable, "-B", DXIL, linked, reflection, dxil)
    if name in DRAWN_WITH:
        checkDrawnWith(dxil, name)
    made = {}
    # Every container carries WGSL, which Maul RHI's writer asks of one
    # using no heap, beside what its own driver reads.
    for suffix, options in (("", ()), (".metal", ("--msl", metal)), (".d3d12", ("--dxil", dxil))):
        container = os.path.join(work, f"{name}{suffix}.mrsc")
        run(sys.executable, "-B", WRITER, *options, linked, wgsl, reflection, container)
        with open(container, "rb") as file:
            made[suffix] = file.read()
    return made


def main():
    if len(sys.argv) in (2, 3) and sys.argv[1] == "--check":
        sys.exit(check(os.path.abspath(sys.argv[2]) if len(sys.argv) == 3 else ROOT))
    flags = strictFlags()
    checkSlang()
    # `--material <source> <directory>`: each linked container built with
    # that material, written to the directory, as the cook asks for a
    # material the blob cannot fold.
    if len(sys.argv) == 4 and sys.argv[1] == "--material":
        shaders = os.path.join(ROOT, "modules", "render_scene_gpu", "shaders")
        with tempfile.TemporaryDirectory() as work:
            for name in LINKED:
                for suffix, data in build(work, shaders, name, os.path.abspath(sys.argv[2])).items():
                    with open(os.path.join(sys.argv[3], f"{name}{suffix}.mrsc"), "wb") as file:
                        file.write(data)
        return
    with tempfile.TemporaryDirectory() as work:
        for module, name in TEST_MATERIALS:
            tests = os.path.join(ROOT, "modules", module, "tests")
            shaders = os.path.join(ROOT, "modules", "render_scene_gpu", "shaders")
            source = os.path.join(tests, "materials", f"{name}.slang")
            generated = os.path.join(tests, "generated")
            os.makedirs(generated, exist_ok=True)
            for linked in LINKED:
                for suffix, data in build(work, shaders, linked, source).items():
                    target = os.path.join(generated, f"{name}.{linked}{suffix}.mrsc")
                    with open(target, "wb") as file:
                        file.write(data)
                    print(f"{target}: {len(data)} bytes")
    with tempfile.TemporaryDirectory() as work:
        for module, name in CONTAINERS:
            shaders = os.path.join(ROOT, "modules", module, "shaders")
            generated = os.path.join(ROOT, "modules", module, "src", "generated")
            os.makedirs(generated, exist_ok=True)
            for suffix, data in build(work, shaders, name).items():
                target = os.path.join(generated, f"{name}{suffix}.mrsc")
                with open(target, "wb") as file:
                    file.write(data)
                print(f"{target}: {len(data)} bytes")
    for directory, names in containers(ROOT).items():
        stamp(directory, names, flags)


if __name__ == "__main__":
    main()
