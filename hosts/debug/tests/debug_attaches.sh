#!/usr/bin/env bash
# An editor debugging the plaza through the debug adapter (D461): a
# dedicated server plays it with the debug grant, and an editor, here a
# script speaking the Debug Adapter Protocol, attaches, breaks at the stroll
# system, is told the game stopped, reads the frame and its arguments,
# carries on, and disconnects, leaving the game running without breakpoints.
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

python3 - "$adapter" "127.0.0.1:$port" "$work/fingerprint" "$work/token" <<'PY'
import json
import subprocess
import sys
import time

adapter, endpoint, pin, token = sys.argv[1:5]
child = subprocess.Popen([adapter], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
seq = 0


def send(command, arguments=None):
    global seq
    seq += 1
    body = json.dumps({"seq": seq, "type": "request", "command": command, "arguments": arguments or {}}).encode()
    child.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    child.stdin.flush()
    return seq


def receive():
    length = 0
    while True:
        line = child.stdout.readline()
        if not line:
            raise SystemExit("the adapter ended")
        line = line.strip()
        if not line:
            break
        if line.startswith(b"Content-Length:"):
            length = int(line.split(b":")[1])
    return json.loads(child.stdout.read(length))


def until(test, what, seconds=20):
    end = time.time() + seconds
    while time.time() < end:
        message = receive()
        if test(message):
            return message
    raise SystemExit("never " + what)


def answer(request):
    return until(lambda m: m.get("type") == "response" and m.get("request_seq") == request, "answered")


initialized = answer(send("initialize", {"adapterID": "rawframe"}))
assert initialized["success"] and initialized["body"]["supportsFunctionBreakpoints"], initialized
attached = send("attach", {"endpoint": endpoint, "pinFile": pin, "tokenFile": token})
assert answer(attached)["success"]
until(lambda m: m.get("event") == "initialized", "initialized")
broken = answer(send("setFunctionBreakpoints", {"breakpoints": [{"name": "stroll"}, {"name": "nowhere"}]}))
assert [each["verified"] for each in broken["body"]["breakpoints"]] == [True, False], broken
lines = answer(send("setBreakpoints", {"source": {"path": "plaza.kest"}, "breakpoints": [{"line": 160}]}))
assert lines["body"]["breakpoints"][0]["verified"] is False, lines
assert answer(send("configurationDone"))["success"]
stopped = until(lambda m: m.get("event") == "stopped", "stopped")
assert stopped["body"]["reason"] == "function breakpoint", stopped
assert answer(send("threads"))["body"]["threads"] == [{"id": 1, "name": "game"}]
stack = answer(send("stackTrace", {"threadId": 1}))
frame = stack["body"]["stackFrames"][0]
assert frame["name"] == "plaza.stroll", stack
scopes = answer(send("scopes", {"frameId": frame["id"]}))
reference = scopes["body"]["scopes"][0]["variablesReference"]
variables = answer(send("variables", {"variablesReference": reference}))["body"]["variables"]
assert variables[0]["name"] == "count" and int(variables[0]["value"]) > 0, variables
assert answer(send("next", {"threadId": 1}))["success"] is False
assert answer(send("continue", {"threadId": 1}))["success"]
# Stopped again at the next tick's stroll: the editor is told again.
until(lambda m: m.get("event") == "stopped", "stopped again")
assert answer(send("disconnect"))["success"]
child.wait(timeout=10)
print("stroll stopped with count", variables[0]["value"])
PY

# Left running: the server is still there to stop.
sleep 1
kill -0 $running
echo "debugged the plaza through the debug adapter"
