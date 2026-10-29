#!/usr/bin/env python3
"""Tests of reap.py: an orphan is tied to its test by the test's name it
carries, and killed only once that test has ended; one that carries no
name is killed by its age, as before. A command stands in for ctest,
running three tests: A forks a helper away from itself (a daemon) and runs
on, B leaves a helper behind and ends at once, and C leaves one that
carries no test's name. Sweeps and grace are shortened to fractions of a
second. Each step waits for the event before it, never for a time: A
ends only once the reaper has named B's and C's helpers, and the command
ends only once A's helper is gone, so no order rests on a margin."""

import os
import signal
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

REAP = Path(__file__).resolve().parent / "reap.py"

# Each helper is `sleep`, double forked so that it is orphaned at once and
# handed to the reaper; its pid is written where the test reads it. A runs
# until the test writes `go`; the command ends once A's helper is gone, or
# after thirty seconds, which fails the test.
COMMAND = r"""
set -u
here="$1"
RAWFRAME_TEST_NAME=A bash -c '
    (sleep 300 & echo $! >"$1/A")
    while [ ! -e "$1/go" ]; do sleep 0.05; done
    kill -0 "$(cat "$1/A")" 2>/dev/null && echo "A: its helper still runs at its end"
' A "$here" &
a=$!
RAWFRAME_TEST_NAME=B bash -c '(sleep 300 & echo $! >"$1/B")' B "$here"
env -u RAWFRAME_TEST_NAME bash -c '(sleep 300 & echo $! >"$1/C")' C "$here"
wait "$a"
for _ in $(seq 600); do
    kill -0 "$(cat "$here/A")" 2>/dev/null || break
    sleep 0.05
done
echo "command: ends"
"""


class Reap(unittest.TestCase):
    def test_an_orphan_lives_while_its_test_runs(self):
        with tempfile.TemporaryDirectory() as here:
            environment = {name: value for name, value in os.environ.items() if name != "RAWFRAME_TEST_NAME"}
            environment.update(REAP_SWEEP_SECONDS="0.2", REAP_ORPHAN_GRACE_SECONDS="1")
            reaper = subprocess.Popen([sys.executable, "-I", str(REAP), "--grace", "1", "bash", "-c", COMMAND,
                                       "command", here], env=environment, stdout=subprocess.PIPE, text=True)
            lines = []

            def read():
                # A ends once the reaper has named both helpers left by B
                # and C, the tests that ended.
                for line in reaper.stdout:
                    lines.append(line.rstrip("\n"))
                    if any("left running by B, killed" in each for each in lines) and any(
                            "left running by a test, killed" in each for each in lines):
                        (Path(here) / "go").touch()

            reading = threading.Thread(target=read)
            reading.start()
            try:
                code = reaper.wait(timeout=60)
            finally:
                if reaper.poll() is None:
                    reaper.kill()
                    reaper.wait()
                (Path(here) / "go").touch()
                reading.join(timeout=5)
                for name in "ABC":
                    if (Path(here) / name).exists():
                        kill(int((Path(here) / name).read_text()))
            output = "\n".join(lines)

            def at(text):
                found = [index for index, line in enumerate(lines) if text in line]
                self.assertTrue(found, f"no line with {text!r} in:\n{output}")
                return found[0]

            self.assertEqual(code, 1, output)
            self.assertLess(at("left running by B, killed"), at("A: its helper still runs at its end"))
            self.assertLess(at("left running by a test, killed"), at("A: its helper still runs at its end"))
            self.assertLess(at("A: its helper still runs at its end"), at("left running by A, killed"))
            self.assertLess(at("left running by A, killed"), at("command: ends"))
            self.assertIn("reap: 3 process(es) outlived the tests that started them", output)


def kill(pid):
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


if __name__ == "__main__":
    unittest.main()
