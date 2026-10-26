#!/usr/bin/env bash
# A running dedicated server paused, stepped, profiled and its traffic read
# through its tooling endpoint, as Studio's Game menu, Profiler and Network
# panel do: runners served to one bot with the inspect and run grants; the
# World paused holds its tick, three steps move it three ticks on, a step
# while it runs is refused, the profile says each tick's and each system's
# time, the traffic counts the bot's connection and what it was sent, and
# let run again the World ticks on.
#
# usage: author_runs.sh <rawframe-author> <rawframe-server> <rawframe-bots> <repository> <work directory>
set -euo pipefail

author=$1
server=$2
bots=$3
repository=$4
work=$5

native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
rm -rf "$work"
mkdir -p "$work"
# A step that fails says where, with what the server and the author said,
# so a failure seen only in a loaded check can be read.
trap 'echo "failed at line $LINENO"; for each in "$work"/*; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
work="$(native "$work")"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
tooling=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
printf '%s\n' "$(python3 -c 'import secrets; print(secrets.token_hex(24))')" >"$work/token"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $(native "$repository")/games/runners/runners.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
tooling.endpoint = 127.0.0.1:$tooling
tooling.token_file = $work/token
tooling.grants = inspect run
CONF
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
kest.plan_only = true
kest.game = $(native "$repository")/games/runners/runners.game
network.quic.pin_file = $work/fingerprint
bots.count = 1
bots.endpoint = 127.0.0.1:$port
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running ${playing:-} 2>/dev/null || true' EXIT
for _ in $(seq 1200); do
    grep -q '"state":"active"' "$work/server.log" 2>/dev/null && break
    sleep 0.05
done
"$bots" --config "$work/bots.conf" >"$work/bots.log" 2>&1 &
playing=$!

# One record asked on a connection of its own, its reply printed: the
# client ends with an error's code where the reply is one, which the reply
# itself says.
ask() {
    { echo "$1" | "$author" connect "127.0.0.1:$tooling" "$work/fingerprint" "$work/token" || true; } | grep '"id":1,'
}
field() {
    python3 -c 'import json, sys; print(json.load(sys.stdin)["answer"][sys.argv[1]])' "$1"
}

# The bot admitted: the traffic counts its connection.
for _ in $(seq 200); do
    [ "$(ask '{"kind":"tooling.traffic","id":1}' | field connections)" -ge 1 ] && break
    sleep 0.1
done
ask '{"kind":"tooling.traffic","id":1}' >"$work/traffic"
python3 -c '
import json, sys
answer = json.load(sys.stdin)["answer"]
assert answer["connections"] == 1, answer
assert answer["stateBytes"] > 0 and answer["recordsSent"] > 0, answer
assert len(answer["perConnection"]) == 1, answer
' <"$work/traffic"

# Paused, the World holds its tick.
ask '{"kind":"tooling.run","id":1,"paused":true}' >"$work/paused"
[ "$(field paused <"$work/paused")" = True ]
held=$(ask '{"kind":"tooling.status","id":1}' | field tick)
sleep 0.5
[ "$(ask '{"kind":"tooling.status","id":1}' | field tick)" -eq "$held" ]

# Three steps, three ticks on, and then held there.
ask '{"kind":"tooling.step","id":1,"ticks":3}' >"$work/stepped"
for _ in $(seq 100); do
    [ "$(ask '{"kind":"tooling.run","id":1}' | field stepsLeft)" -eq 0 ] && break
    sleep 0.05
done
[ "$(ask '{"kind":"tooling.status","id":1}' | field tick)" -eq $((held + 3)) ]

# Each tick's time and each system's.
ask '{"kind":"tooling.profile","id":1}' >"$work/profile"
python3 -c '
import json, sys
answer = json.load(sys.stdin)["answer"]
assert answer["tickUs"] == 16666, answer
assert len(answer["ticksUs"]) > 0, answer
systems = {s["system"]: s for s in answer["systems"]}
# The game runs its own Kest systems among the engine ones, each timed: its age system
# ran every tick, so it took time, and no one run took more than all.
assert "runners.age" in systems and "rawframe.replication.publish" in systems, answer
assert systems["runners.age"]["totalUs"] > 0, answer
assert all(s["mostUs"] <= s["totalUs"] for s in systems.values()), answer
' <"$work/profile"

# Let run, the World ticks on, and a step is refused while it does.
ask '{"kind":"tooling.run","id":1,"paused":false}' >"$work/resumed"
[ "$(field paused <"$work/resumed")" = False ]
ask '{"kind":"tooling.step","id":1,"ticks":1}' | grep -q '"error":'
# Two seconds: a loaded machine may hold the World a half second.
sleep 2
(( $(ask '{"kind":"tooling.status","id":1}' | field tick) > held + 3 ))
echo "paused, stepped, profiled and read runners' traffic through the tooling endpoint"
