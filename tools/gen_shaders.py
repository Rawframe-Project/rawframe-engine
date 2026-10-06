#!/usr/bin/env python3
# Builds the engine's shader containers offline, as a cook would: no
# shipped build compiles a shader (ADR-0029). glslangValidator compiles
# each stage under its entry name, spirv-link joins them into one module,
# spirv-val checks it for Vulkan 1.3, and Maul RHI's container writer
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
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WRITER = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_container.py")
METAL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_msl.py")
DXIL = os.path.join(ROOT, "third_party", "maul-rhi", "tools", "mrhi_dxil.py")
# Each container: its module, its name, and its stages' sources and entries,
# with any names a stage is compiled with defined.
CONTAINERS = (
    ("render", "display", (("vert", "vs"), ("frag", "fs"))),
    ("render_canvas_gpu", "sprite", (("vert", "vs"), ("frag", "fs"))),
    ("render_canvas_gpu", "ui", (("vert", "vs"), ("frag", "fs"), ("image.vert", "imageVs"), ("image.frag", "imageFs"),
                                  ("shadow.vert", "shadowVs"), ("shadow.frag", "shadowFs"), ("glyph.vert", "glyphVs"),
                                  ("glyph.frag", "glyphFs"))),
    ("render_scene_gpu", "scene", (("vert", "vs"), ("frag", "fs"), ("frag", "fsDecaled", "DECALS"),
                                   ("cut.frag", "cut"), ("normal.frag", "normal"), ("cutnormal.frag", "cutNormal"))),
    ("render_scene_gpu", "occlusion", (("vert", "vs"), ("frag", "occlude"), ("blur.frag", "blur"))),
    ("render_scene_gpu", "reflect", (("vert", "vs"), ("frag", "march"))),
    ("render_scene_gpu", "motion", (("vert", "vs"), ("tile.frag", "tile"), ("neighbor.frag", "neighbor"), ("gather.frag", "gather"))),
    ("render_scene_gpu", "focus", (("vert", "vs"), ("prefilter.frag", "prefilter"), ("bokeh.frag", "bokeh"), ("combine.frag", "combine"))),
    ("render_scene_gpu", "contact", (("vert", "vs"), ("frag", "shade"))),
    ("render_scene_gpu", "decal", (("vert", "vs"), ("frag", "fill"))),
    ("render_scene_gpu", "probe", (("vert", "vs"), ("frag", "fill"))),
    ("render_scene_gpu", "resolve", (("vert", "vs"), ("frag", "depth"))),
    ("render_scene_gpu", "fxaa", (("vert", "vs"), ("frag", "fs"))),
    ("render_scene_gpu", "bloom", (("vert", "vs"), ("first.frag", "first"), ("down.frag", "down"), ("up.frag", "up"))),
    ("render_scene_gpu", "meter", (("histogram.comp", "histogram"), ("adapt.comp", "adapt"))),
    ("render_scene_gpu", "shadow", (("vert", "vs"), ("cut.vert", "vsCut"), ("cut.frag", "cut"))),
    ("render_scene_gpu", "sky", (("vert", "vs"), ("frag", "fs"))),
    ("render_scene_gpu", "temporal", (("vert", "vs"), ("frag", "fs"))),
    ("render_scene_gpu", "tonemap", (("vert", "vs"), ("frag", "fs"))),
    ("render_scene_gpu", "post", (("vert", "vs"), ("frag", "fs"))),
    ("particles_gpu", "spawn", (("comp", "spawn"), ("comp", "clear", "CLEAR"))),
    ("particles_gpu", "particle", (("vert", "vs"), ("ribbon.vert", "ribbon"), ("frag", "fs"))),
)


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def build(work, shaders, name, stages):
    modules = []
    for suffix, entry, *defines in stages:
        module = os.path.join(work, f"{name}_{entry}.spv")
        run("glslangValidator", "-V", "--target-env", "vulkan1.3", *(f"-D{define}" for define in defines), "-e",
            entry, "--source-entrypoint", "main", "-o", module, os.path.join(shaders, f"{name}.{suffix}"))
        modules.append(module)
    linked = os.path.join(work, f"{name}.spv")
    run("spirv-link", "--target-env", "vulkan1.3", *modules, "-o", linked)
    run("spirv-val", "--target-env", "vulkan1.3", linked)
    reflection = os.path.join(shaders, f"{name}.json")
    metal = os.path.join(work, f"{name}_metal")
    run(sys.executable, METAL, linked, reflection, metal)
    dxil = os.path.join(work, f"{name}_dxil")
    run(sys.executable, DXIL, linked, reflection, dxil)
    wgsl = os.path.join(shaders, f"{name}.wgsl")
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
    with tempfile.TemporaryDirectory() as work:
        for module, name, stages in CONTAINERS:
            shaders = os.path.join(ROOT, "modules", module, "shaders")
            generated = os.path.join(ROOT, "modules", module, "src", "generated")
            os.makedirs(generated, exist_ok=True)
            for suffix, data in build(work, shaders, name, stages).items():
                target = os.path.join(generated, f"{name}{suffix}.mrsc")
                with open(target, "wb") as file:
                    file.write(data)
                print(f"{target}: {len(data)} bytes")


if __name__ == "__main__":
    main()
