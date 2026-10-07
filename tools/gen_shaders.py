#!/usr/bin/env python3
# Builds the engine's shader containers offline, as a cook would: no
# shipped build compiles a shader (ADR-0029). Each container's source is
# NAME.slang, Slang being the one first-party GPU language (ADR-0026,
# D475): slangc compiles every entry into one SPIR-V module and into WGSL,
# from the one source; the pinned compiler's version is checked, and
# RAWFRAME_SLANGC names it where it is not on the path. spirv-val checks
# the module for Vulkan 1.3, and Maul RHI's container writer
# (third_party/maul-rhi/tools/mrhi_container.py) writes the containers a
# build's driver reads (D416), each with the SPIR-V and each entry in WGSL,
# which the writer asks of every container: NAME.mrsc, for Vulkan and
# WebGPU; NAME.metal.mrsc, with each entry in Metal's language too, crossed
# from the SPIR-V by Maul RHI's mrhi_msl.py through SPIRV-Cross (D406); and
# NAME.d3d12.mrsc, with each entry's DXIL, crossed to HLSL and compiled by
# DXC through its mrhi_dxil.py (D415). They land in the module's
# src/generated, and the build embeds the one its driver reads
# (rawframe_shader_containers in cmake/rawframe.cmake). The check runs none
# of these tools; the containers are committed, and this is run again
# whenever a source changes (D278). spirv-cross and dxc must be on the path.
#
# usage: tools/gen_shaders.py

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WRITER = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_container.py")
METAL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_msl.py")
DXIL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_dxil.py")
# The Slang compiler every container is built with: its exact release, as
# the language is defined by the pinned compiler until it is ratified
# (ADR-0026).
SLANGC = os.environ.get("RAWFRAME_SLANGC", "slangc")
SLANG_VERSION = "2026.19"
# The containers whose vertex position must be invariant (D475).
INVARIANT = ("scene",)
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
    ("render_scene_gpu", "shadow"),
    ("render_scene_gpu", "sky"),
    ("render_scene_gpu", "temporal"),
    ("render_scene_gpu", "tonemap"),
    ("render_scene_gpu", "post"),
    ("particles_gpu", "spawn"),
    ("particles_gpu", "particle"),
)


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def invariantPosition(spirv, wgsl):
    # Slang 2026.19 cannot say a vertex position is invariant, which the
    # scene's depth prepass and lit pass need to meet exactly (D475): the
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


def checkSlang():
    result = subprocess.run([SLANGC, "-version"], capture_output=True, text=True)
    version = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or version != SLANG_VERSION:
        sys.exit(f"{SLANGC} is {version or 'missing'}; the containers are built with Slang {SLANG_VERSION}")


def build(work, shaders, name):
    linked = os.path.join(work, f"{name}.spv")
    source = os.path.join(shaders, f"{name}.slang")
    # Entries of one container may read one slot as different records (a
    # particle's emitter, a ribbon's range): a pipeline holds only its own
    # entries, so Slang's overlap warning (39001) is no fault. A source
    # imports the modules beside it (screen.slang).
    quiet = ("-warnings-disable", "39001", "-I", shaders)
    run(SLANGC, source, *quiet, "-target", "spirv", "-profile", "spirv_1_6", "-fvk-use-entrypoint-name", "-o", linked)
    wgsl = os.path.join(work, f"{name}.wgsl")
    run(SLANGC, source, *quiet, "-target", "wgsl", "-o", wgsl)
    if name in INVARIANT:
        invariantPosition(linked, wgsl)
    run("spirv-val", "--target-env", "vulkan1.3", linked)
    reflection = os.path.join(shaders, f"{name}.json")
    metal = os.path.join(work, f"{name}_metal")
    run(sys.executable, METAL, linked, reflection, metal)
    dxil = os.path.join(work, f"{name}_dxil")
    run(sys.executable, DXIL, linked, reflection, dxil)
    made = {}
    # Every container carries WGSL, which Maul RHI's writer asks of one
    # using no heap, beside what its own driver reads.
    for suffix, options in (("", ()), (".metal", ("--msl", metal)), (".d3d12", ("--dxil", dxil))):
        container = os.path.join(work, f"{name}{suffix}.mrsc")
        run(sys.executable, WRITER, *options, linked, wgsl, reflection, container)
        with open(container, "rb") as file:
            made[suffix] = file.read()
    return made


def main():
    checkSlang()
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


if __name__ == "__main__":
    main()
