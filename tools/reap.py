#!/usr/bin/env python3
"""Runs a command (ctest) as the subreaper of everything it starts, so a
process a test leaves behind cannot outlive the run.

A test that ends while a server, client or helper it started is still
running orphans that process. On Linux the orphan is handed to this
process instead of to init. Every test carries its name in
RAWFRAME_TEST_NAME (the root CMakeLists.txt sets it), and so does every
process it starts, which is how an orphan is tied to its test: a helper a
running test forked away from itself (a daemon) is that test's, and is
left alone while any process of the test still runs under the command.
Every 30 seconds, an orphan whose test has ended for more than a minute,
or one that carries no test's name and was handed here more than a
minute before, is named with its test and killed (SIGKILL, by the exact
pid, which nothing else can have reused while it is this process's
child); so is any still running `--grace` seconds (ten unless given)
after the command ends, and then any child such a process leaves in turn. A child
that ends is reaped at once, as init would: a test waiting for a process
it orphaned to go must not see it linger as a zombie. A stopped server
ignores stop signals while its leak check runs, so SIGKILL is the one that
ends one stuck there.

Exits with the command's status, or 1 when anything was left behind.
Elsewhere, or where the system refuses it a subreaper, it says so and
only runs the command.

usage: reap.py [--grace <seconds>] <command> [arguments...]
"""

import ctypes
import os
import signal
import subprocess
import sys
import time

PR_SET_CHILD_SUBREAPER = 36
# tools/reap_test.py shortens both.
SWEEP_SECONDS = float(os.environ.get("REAP_SWEEP_SECONDS", "30"))
ORPHAN_GRACE_SECONDS = float(os.environ.get("REAP_ORPHAN_GRACE_SECONDS", "60"))
MARKER = b"RAWFRAME_TEST_NAME="
# Once the command ends, what is still finishing (a server's leak check)
# has this long to end of itself, unless `--grace` says otherwise.
LAST_GRACE_SECONDS = 10
# A killed process's own children are handed here as it goes, and theirs
# after them: the last pass is repeated until none is left, at most this
# many times.
LAST_ROUNDS = 16


def children(of: int, besides: int) -> list[int]:
    """The live pids whose parent is `of`, read from /proc, `besides` left
    out; a child that has ended is reaped instead."""
    found = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/stat", "rb") as stat:
                fields = stat.read().rsplit(b")", 1)[1].split()
        except OSError:
            continue
        if int(fields[1]) != of or int(entry) == besides:
            continue
        if fields[0] == b"Z":
            try:
                os.waitpid(int(entry), os.WNOHANG)
            except ChildProcessError:
                pass
            continue
        found.append(int(entry))
    return found


def parents() -> dict[int, int]:
    """Every live process's parent, read from /proc."""
    found = {}
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/stat", "rb") as stat:
                found[int(entry)] = int(stat.read().rsplit(b")", 1)[1].split()[1])
        except (OSError, IndexError, ValueError):
            continue
    return found


def test_of(pid: int) -> str | None:
    """The test `pid` was started for, by the name it carries; None for a
    process that carries none."""
    try:
        with open(f"/proc/{pid}/environ", "rb") as environ:
            variables = environ.read().split(b"\0")
    except OSError:
        return None
    for variable in variables:
        if variable.startswith(MARKER):
            return variable[len(MARKER):].decode(errors="replace")
    return None


def running_tests(command: int) -> set[str]:
    """The tests a process under `command` still runs for: an orphan, a
    child of this process, never counts, so a test's own helper does not
    keep it running."""
    table = parents()
    under: dict[int, bool] = {command: True}

    def descends(pid: int) -> bool:
        chain = []
        while pid not in under:
            chain.append(pid)
            pid = table.get(pid, 0)
            if pid <= 1:
                under[pid] = False
                break
        for each in chain:
            under[each] = under[pid]
        return under[pid]

    tests = set()
    for pid in table:
        if pid != command and descends(pid):
            if (test := test_of(pid)) is not None:
                tests.add(test)
    return tests


def command_line(pid: int) -> str:
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as cmdline:
            return cmdline.read().replace(b"\0", b" ").decode(errors="replace").strip()[:300]
    except OSError:
        return "?"


def end(pid: int) -> None:
    test = test_of(pid)
    by = test if test is not None else "a test"
    print(f"reap: left running by {by}, killed: {pid} {command_line(pid)}", flush=True)
    try:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
    except (ProcessLookupError, ChildProcessError):
        pass


def main() -> int:
    arguments = sys.argv[1:]
    grace = LAST_GRACE_SECONDS
    if arguments[:1] == ["--grace"] and len(arguments) >= 2:
        grace = float(arguments[1])
        arguments = arguments[2:]
    if not arguments:
        print(__doc__, file=sys.stderr)
        return 2
    if not sys.platform.startswith("linux"):
        print("reap: not Linux, so what the tests leave running is not reaped", flush=True)
        return subprocess.call(arguments)
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        print(f"reap: no subreaper ({os.strerror(ctypes.get_errno())}), so what the tests leave running is not reaped",
              flush=True)
        return subprocess.call(arguments)
    me = os.getpid()
    ended = [False]
    signal.signal(signal.SIGCHLD, lambda *_: ended.__setitem__(0, True))
    command = subprocess.Popen(arguments)
    first_seen: dict[int, float] = {}
    left = 0
    swept = time.monotonic()
    while True:
        try:
            status = command.wait(timeout=0.1)
            break
        except subprocess.TimeoutExpired:
            pass
        if ended[0]:
            ended[0] = False
            children(me, command.pid)
        now = time.monotonic()
        if now - swept < SWEEP_SECONDS:
            continue
        swept = now
        orphans = children(me, command.pid)
        running = running_tests(command.pid)
        # An orphan's grace runs from when its test was last seen running,
        # or, carrying no test's name, from when it was handed here.
        first_seen = {
            pid: now if test_of(pid) in running else first_seen.get(pid, now) for pid in orphans
        }
        for pid, since in list(first_seen.items()):
            if now - since >= ORPHAN_GRACE_SECONDS:
                end(pid)
                del first_seen[pid]
                left += 1
    deadline = time.monotonic() + grace
    while children(me, command.pid) and time.monotonic() < deadline:
        time.sleep(0.2)
    for _ in range(LAST_ROUNDS):
        remaining = children(me, command.pid)
        if not remaining:
            break
        for pid in remaining:
            end(pid)
            left += 1
    else:
        # What still runs is named, so nothing outlives the run unnamed.
        if still := children(me, command.pid):
            print(f"reap: still running after {LAST_ROUNDS} rounds: {' '.join(map(str, still))}", flush=True)
    if left:
        print(f"reap: {left} process(es) outlived the tests that started them", flush=True)
        return status or 1
    return status


if __name__ == "__main__":
    sys.exit(main())
