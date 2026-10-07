#!/usr/bin/env bash
# An editor debugging the plaza through the debug adapter (D461): a
# dedicated server plays it with the debug grant, and an editor, here a
# script speaking the Debug Adapter Protocol, attaches, breaks at the stroll
# system, is told the game stopped, reads the frame and its arguments,
# carries on, and disconnects, leaving the game running without breakpoints.
# An endpoint off this machine is refused, and a play directory others may
# enter; the game alone, through where Studio's Play records it (D462),
# attaches too.
#
# usage: debug_attaches.sh <rawframe-debug> <rawframe-server> <repository>
#                          <content settings> <work directory>
set -euo pipefail

adapter=$1
server=$2
repository=$3
content=$4
work=$5

rm -rf "$work"
mkdir -p "$work"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/token"
cat "$content" >"$work/server.conf"
cat >>"$work/server.conf" <<CONF
host.maximum_iterations = 6000
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $repository/games/plaza/plaza.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
tooling.grants = inspect debug
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running 2>/dev/null || true' EXIT
for _ in $(seq 200); do
    [[ -s "$work/fingerprint" ]] && break
    sleep 0.1
done

# Where Studio's Play would have recorded the game, as it names the
# directory (D462): under the user's runtime directory, by the digest of
# the game's absolute path, its owner's alone.
game="$repository/games/plaza/plaza.game"
export XDG_RUNTIME_DIR="$work/runtime"
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
played="$XDG_RUNTIME_DIR/rawframe-play-$(python3 -c 'import hashlib, os, sys; print(hashlib.sha256(os.path.normpath(os.path.abspath(sys.argv[1])).encode()).hexdigest()[:32])' "$game")"
mkdir -p -m 755 "$played"
printf '{"endpoint":"127.0.0.1:%s","pinFile":"%s","tokenFile":"%s"}\n' "$port" "$work/fingerprint" "$work/token" >"$played/debug.attach"

python3 - "$adapter" "127.0.0.1:$port" "$work/fingerprint" "$work/token" "$game" "$played" "$work/server.log" <<'PY'
import json
import os
import select
import subprocess
import sys
import time

adapter, endpoint, pin, token, game, played, server_log = sys.argv[1:8]


def explain(seen):
    """What a failed wait saw, and the server's last words, so a failure
    seen only in a loaded check says why (D478): the adapter's messages
    the wait passed over, whether the server is still there, and its last
    records."""
    print("passed over:", " ".join(seen[-20:]) or "nothing", flush=True)
    with open(server_log, encoding="utf-8", errors="replace") as records:
        lines = records.read().splitlines()
    for line in lines[-12:]:
        try:
            record = json.loads(line)
            said = "%s.%s: %s %s" % (record.get("domain"), record.get("code"), record.get("message"),
                                     json.dumps(record.get("fields", {}))[:200])
        except ValueError:
            said = line[:200]
        print("server:", said, flush=True)


class Editor:
    """An editor's side of the protocol, over one adapter."""

    def __init__(self):
        # Unbuffered, so what select says waits is all there is to read.
        self.child = subprocess.Popen([adapter], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        self.seq = 0
        self.seen = []

    def send(self, command, arguments=None):
        self.seq += 1
        body = json.dumps({"seq": self.seq, "type": "request", "command": command,
                           "arguments": arguments or {}}).encode()
        self.child.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.child.stdin.flush()
        return self.seq

    def receive(self, end, what):
        # A silent adapter fails the step it was in, never hangs the test.
        if not select.select([self.child.stdout], [], [], max(0.0, end - time.time()))[0]:
            # Where each of its threads waits, for a hang seen only in a
            # loaded check (D474).
            tasks = "/proc/%d/task" % self.child.pid
            for task in sorted(os.listdir(tasks)) if os.path.isdir(tasks) else []:
                with open("%s/%s/wchan" % (tasks, task)) as wchan:
                    print("adapter thread", task, wchan.read(), flush=True)
            explain(self.seen)
            raise SystemExit("the adapter said nothing in time, waiting to be " + what)
        length = 0
        while True:
            line = self.child.stdout.readline()
            if not line:
                raise SystemExit("the adapter ended")
            line = line.strip()
            if not line:
                break
            if line.startswith(b"Content-Length:"):
                length = int(line.split(b":")[1])
        body = b""
        while len(body) < length:
            more = self.child.stdout.read(length - len(body))
            if not more:
                raise SystemExit("the adapter ended")
            body += more
        return json.loads(body)

    # What the adapter tells waits on the game, which on a loaded machine
    # may take a while to run a tick; a minute still fails a silent one
    # well inside the test's own limit (D476).
    def until(self, test, what, seconds=60):
        end = time.time() + seconds
        while time.time() < end:
            message = self.receive(end, what)
            if test(message):
                return message
            self.seen.append(message.get("event") or message.get("command") or message.get("type", "?"))
        explain(self.seen)
        raise SystemExit("never " + what)

    def answer(self, request, seconds=60):
        return self.until(lambda m: m.get("type") == "response" and m.get("request_seq") == request, "answered",
                          seconds)

    def attach(self, arguments):
        initialized = self.answer(self.send("initialize", {"adapterID": "rawframe"}))
        assert initialized["success"] and initialized["body"]["supportsFunctionBreakpoints"], initialized
        # The adapter waits up to ten seconds to connect and ten for the
        # game's welcome, then answers a refusal itself.
        return self.answer(self.send("attach", arguments))

    def leave(self):
        assert self.answer(self.send("disconnect"))["success"]
        try:
            self.child.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.child.kill()
            raise SystemExit("the adapter did not end after disconnect")


# A token goes only to this machine: an endpoint elsewhere is refused
# before anything is sent.
remote = Editor()
refused = remote.attach({"endpoint": "10.0.0.1:" + endpoint.split(":")[1], "pinFile": pin, "tokenFile": token})
assert not refused["success"] and "loopback" in refused["message"], refused
remote.leave()
print("a remote endpoint refused", flush=True)

# A play directory others may enter is not trusted.
open_to_others = Editor()
refused = open_to_others.attach({"game": game})
assert not refused["success"] and "open to others" in refused["message"], refused
open_to_others.leave()
print("a play directory open to others refused", flush=True)
os.chmod(played, 0o700)

editor = Editor()
attached = editor.attach({"endpoint": endpoint, "pinFile": pin, "tokenFile": token})
assert attached["success"], attached
editor.until(lambda m: m.get("event") == "initialized", "initialized")
broken = editor.answer(editor.send("setFunctionBreakpoints", {"breakpoints": [{"name": "stroll"}, {"name": "nowhere"}]}))
assert [each["verified"] for each in broken["body"]["breakpoints"]] == [True, False], broken
lines = editor.answer(editor.send("setBreakpoints", {"source": {"path": "plaza.kest"}, "breakpoints": [{"line": 160}]}))
assert lines["body"]["breakpoints"][0]["verified"] is False, lines
assert editor.answer(editor.send("configurationDone"))["success"]
stopped = editor.until(lambda m: m.get("event") == "stopped", "stopped")
assert stopped["body"]["reason"] == "function breakpoint", stopped
assert editor.answer(editor.send("threads"))["body"]["threads"] == [{"id": 1, "name": "game"}]
stack = editor.answer(editor.send("stackTrace", {"threadId": 1}))
frame = stack["body"]["stackFrames"][0]
assert frame["name"] == "plaza.stroll", stack
scopes = editor.answer(editor.send("scopes", {"frameId": frame["id"]}))
reference = scopes["body"]["scopes"][0]["variablesReference"]
variables = editor.answer(editor.send("variables", {"variablesReference": reference}))["body"]["variables"]
assert variables[0]["name"] == "count" and int(variables[0]["value"]) > 0, variables
assert editor.answer(editor.send("next", {"threadId": 1}))["success"] is False
assert editor.answer(editor.send("continue", {"threadId": 1}))["success"]
# Stopped again at the next tick's stroll: the editor is told again.
editor.until(lambda m: m.get("event") == "stopped", "stopped again")
editor.leave()
print("stroll stopped with count", variables[0]["value"], flush=True)

# Attached by the game alone, through where Play recorded it.
by_game = Editor()
attached = by_game.attach({"game": game})
assert attached["success"], attached
by_game.until(lambda m: m.get("event") == "initialized", "initialized")
broken = by_game.answer(by_game.send("setFunctionBreakpoints", {"breakpoints": [{"name": "plaza.stroll"}]}))
assert broken["body"]["breakpoints"][0]["verified"], broken
by_game.until(lambda m: m.get("event") == "stopped", "stopped through the game")
by_game.leave()
print("attached by the game through its play directory")
PY

# Left running: the server is still there to stop.
sleep 1
kill -0 $running
echo "debugged the plaza through the debug adapter"
