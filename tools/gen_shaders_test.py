#!/usr/bin/env python3
"""Tests of gen_shaders.py's stamp: on a copy of the containers, their
stamps and what --check compares them with, the current tree passes, and
each way a container can stop being the cook the stamp names fails, as does
a cook with a Maul RHI whose mrhi_dxil.py passes no -Gis. None needs a
shader tool."""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DXIL = Path("third_party/maul-rhi/tools/mrhi_dxil.py")
BASE = Path("third_party/maul-rhi/include/maul-rhi/base.h")
STAMP = Path("modules/render/src/generated/containers.stamp")
CONTAINER = Path("modules/render/src/generated/display.d3d12.mrsc")


class Stamp(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.copy = Path(cls.work.name)
        for path in (Path("tools/gen_shaders.py"), DXIL, BASE):
            (cls.copy / path).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, cls.copy / path)
        for directory in ROOT.glob("modules/*/*/generated"):
            if any(directory.glob("*.mrsc")):
                shutil.copytree(directory, cls.copy / directory.relative_to(ROOT))

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def check(self):
        return subprocess.run([sys.executable, "-B", str(self.copy / "tools/gen_shaders.py"), "--check"],
                              capture_output=True, text=True)

    def changed(self, path, change):
        """Runs --check with `change` applied to the copy's `path`, then
        puts the file back; returns the check's result."""
        target = self.copy / path
        kept = target.read_bytes()
        try:
            change(target)
            return self.check()
        finally:
            target.write_bytes(kept)

    def assertRefused(self, result, words):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(words, result.stdout)

    def test_the_current_containers_pass(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_an_edited_container_is_refused(self):
        def flip(target):
            data = bytearray(target.read_bytes())
            data[-1] ^= 1
            target.write_bytes(bytes(data))
        self.assertRefused(self.changed(CONTAINER, flip), "not the bytes containers.stamp names")

    def test_a_missing_container_is_refused(self):
        self.assertRefused(self.changed(CONTAINER, os.remove), "display.d3d12.mrsc: missing")

    def test_a_stamp_without_gis_is_refused(self):
        def without(target):
            target.write_text(target.read_text().replace("dxc-flags -Gis ", "dxc-flags ", 1))
        self.assertRefused(self.changed(STAMP, without), "compiled without -Gis")

    def test_a_missing_stamp_is_refused(self):
        self.assertRefused(self.changed(STAMP, os.remove), "containers.stamp: missing")

    def test_other_vendored_flags_are_refused(self):
        def other(target):
            target.write_text(re.sub(r"^DXC_FLAGS = .*$", 'DXC_FLAGS = ("-Gis", "-Qstrip_debug")',
                                     target.read_text(), count=1, flags=re.M))
        self.assertRefused(self.changed(DXIL, other), "the vendored mrhi_dxil.py passes -Gis -Qstrip_debug")

    def test_another_maul_rhi_release_is_refused(self):
        def moved(target):
            target.write_text(re.sub(r"^#define MRHI_VERSION_PATCH \d+$", "#define MRHI_VERSION_PATCH 99",
                                     target.read_text(), count=1, flags=re.M))
        self.assertRefused(self.changed(BASE, moved), "the vendored one is")

    def test_a_cook_without_gis_is_refused_before_any_tool(self):
        def loose(target):
            target.write_text(re.sub(r"^DXC_FLAGS = .*$", 'DXC_FLAGS = ("-Qstrip_reflect", "-Qstrip_debug")',
                                     target.read_text(), count=1, flags=re.M))
        target = self.copy / DXIL
        kept = target.read_bytes()
        try:
            loose(target)
            result = subprocess.run([sys.executable, "-B", str(self.copy / "tools/gen_shaders.py")],
                                    capture_output=True, text=True, env={**os.environ, "PATH": ""})
        finally:
            target.write_bytes(kept)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("without -Gis", result.stderr)


if __name__ == "__main__":
    unittest.main()
