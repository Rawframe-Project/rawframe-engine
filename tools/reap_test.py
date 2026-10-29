#!/usr/bin/env python3
"""Tests of reap.py: an orphan is tied to its test by the test's name it
carries, and killed only once that test has ended; one that carries no
name is killed by its age, as before. A command stands in for ctest,
running three tests: A forks a helper away from itself (a daemon) and runs
on a while, B leaves a helper behind and ends at once, and C leaves one
that carries no test's name. Sweeps and grace are shortened to fractions of
a second."""

import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REAP = Path(__file__).resolve().parent / "reap.py"

# Each helper is `sleep`, double forked so that it is orphaned at once and
# handed to the reaper; its pid is written where the test reads it.
COMMAND = r"""
set -u
here="$1"
RAWFRAME_TEST_NAME=A bash -c '
    (sleep 300 & echo $! >"$1/A")
    sleep 3
    kill -0 "$(cat "$1/A")" 2>/dev/null && echo "A: its helper still runs at its end"
' A "$here" &
a=$!
RAWFRAME_TEST_NAME=B bash -c '(sleep 300 & echo $! >"$1/B")' B "$here"
env -u RAWFRAME_TEST_NAME bash -c '(sleep 300 & echo $! >"$1/C")' C "$here"
wait "$a"
sleep 2
echo "command: ends"
"""


class Reap(unittest.TestCase):
    def test_an_orphan_lives_while_its_test_runs(self):
        with tempfile.TemporaryDirectory() as here:
            environment = {name: value for name, value in os.environ.items() if name != "RAWFRAME_TEST_NAME"}
            environment.update(REAP_SWEEP_SECONDS="0.2", REAP_ORPHAN_GRACE_SECONDS="1")
            done = subprocess.run([sys.executable, "-I", str(REAP), "--grace", "1", "bash", "-c", COMMAND, "command",
                                   here], env=environment, capture_output=True, text=True, timeout=60)
            for name in "ABC":
                if (Path(here) / name).exists():
                    self.addCleanup(kill, int((Path(here) / name).read_text()))
            lines = done.stdout.splitlines()

            def at(text):
                found = [index for index, line in enumerate(lines) if text in line]
                self.assertTrue(found, f"no line with {text!r} in:\n{done.stdout}")
                return found[0]

            self.assertEqual(done.returncode, 1, done.stdout)
            # A's helper outlived the minute's stand-in, A being still running,
            # and was killed after A ended, named with A, before the end.
            self.assertLess(at("left running by B, killed"), at("A: its helper still runs at its end"))
            self.assertLess(at("left running by a test, killed"), at("A: its helper still runs at its end"))
            self.assertLess(at("A: its helper still runs at its end"), at("left running by A, killed"))
            self.assertLess(at("left running by A, killed"), at("command: ends"))
            self.assertIn("reap: 3 process(es) outlived the tests that started them", done.stdout)


def kill(pid):
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


if __name__ == "__main__":
    unittest.main()
