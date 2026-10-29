#!/usr/bin/env python3
"""Tests of how click.py ends: the program it watches, and every process
that program started in its group, stop when click.py does, however its
steps ended, so none stays on the machine. A stand-in plays Studio here,
started in a group of its own as the Studio scripts start it, with a child
that ignores the request to stop, as a hung played server or client would.
click.py runs with no X display, so its first step fails."""

import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

CLICK = Path(__file__).resolve().parent / "click.py"

# `{parent}` is what the stand-in does on SIGTERM: '' ignores it.
STAND_IN = """trap '{parent}' TERM
bash -c "trap '' TERM; exec sleep 300" &
echo $! >"$1.part"
mv "$1.part" "$1"
wait
"""


def gone(pid, seconds=5):
    """Whether `pid` ends within `seconds`; a killed orphan is reaped by
    whoever adopts it, a moment later."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        time.sleep(0.05)
    return False


def kill(pid):
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


class Finish(unittest.TestCase):
    def ends(self, parent):
        """Runs click.py against a stand-in that does `parent` on SIGTERM;
        returns click.py's code, the stand-in's, and its child's pid."""
        with tempfile.TemporaryDirectory() as work:
            child_file = Path(work) / "child"
            stand_in = subprocess.Popen(["bash", "-c", STAND_IN.format(parent=parent), "stand-in", str(child_file)],
                                        start_new_session=True)
            click = None
            try:
                while not child_file.exists():
                    time.sleep(0.05)
                child = int(child_file.read_text(encoding="utf-8"))
                # What click.py failed to stop is stopped after the test's
                # checks, so a failing test leaves nothing running either.
                self.addCleanup(kill, child)
                pid_file = Path(work) / "pid"
                pid_file.write_text(str(stand_in.pid), encoding="utf-8")
                environment = {name: value for name, value in os.environ.items() if name != "DISPLAY"}
                environment["CLICK_STOP_SECONDS"] = "1"
                click = subprocess.Popen(
                    [sys.executable, "-I", str(CLICK), str(stand_in.pid), os.devnull, "shown", str(pid_file), "1,1"],
                    env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                # This process reaps the stand-in, as the Studio scripts'
                # shell does, while click.py waits for it.
                deadline = time.monotonic() + 30
                while click.poll() is None and time.monotonic() < deadline:
                    stand_in.poll()
                    time.sleep(0.05)
                self.assertIsNotNone(click.poll(), "click.py never ended")
                return click.returncode, stand_in.wait(timeout=5), child
            finally:
                if stand_in.poll() is None:
                    os.killpg(stand_in.pid, signal.SIGKILL)
                    stand_in.wait()
                if click is not None and click.poll() is None:
                    click.kill()
                    click.wait()

    def test_a_program_that_obeys_loses_a_child_that_does_not(self):
        code, stand_in, child = self.ends("exit 0")
        self.assertNotEqual(code, 0)
        self.assertEqual(stand_in, 0)
        self.assertTrue(gone(child), "the child that ignored its stop outlived click.py")

    def test_a_program_that_ignores_its_stop_is_killed_with_its_child(self):
        code, stand_in, child = self.ends("")
        self.assertNotEqual(code, 0)
        self.assertEqual(stand_in, -signal.SIGKILL)
        self.assertTrue(gone(child), "the child outlived click.py")


if __name__ == "__main__":
    unittest.main()
