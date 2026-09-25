#!/usr/bin/env bash
# A dedicated server drains (SPEC-0012): asked to stop while bots play, it
# closes admission, refuses a second bots process as unavailable, keeps
# serving the first until those bots leave, and only then stops, well
# within its drain time. Run from the repository root.
#
#   drain.sh <rawframe-server> <rawframe-bots>
set -euo pipefail

server="$1"
bots="$2"
work="$(mktemp -d)"
pids=()
trap 'kill "${pids[@]}" 2>/dev/null || true; rm -rf "$work"' EXIT

port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
host.drain_ms = 60000
world.tick_rate = 60
kest.game = games/arena/arena.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
bots_conf() {
    cat <<CONF
host.maximum_iterations = $1
host.iteration_rate = 120
kest.plan_only = true
kest.game = games/arena/arena.game
network.quic.pin_file = $work/fingerprint
bots.count = 2
bots.endpoint = 127.0.0.1:$port
CONF
}
bots_conf 0 >"$work/playing.conf"
bots_conf 360 >"$work/late.conf"

# Waits until a log names a code or a state, or fails after thirty seconds.
await() {
    for _ in $(seq 600); do
        grep -q "$2" "$1" 2>/dev/null && return 0
        sleep 0.05
    done
    echo "timed out waiting for $2 in $1" >&2
    return 1
}

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
pids+=("$server_pid")
await "$work/server.log" '"code":"listening"'

# The first bots play until they are told to stop.
"$bots" --config "$work/playing.conf" >"$work/playing.log" 2>&1 &
playing_pid=$!
pids+=("$playing_pid")
await "$work/playing.log" '"code":"bots_admitted"'

# The server drains, the bots still connected.
kill -TERM "$server_pid"
await "$work/server.log" '"state":"draining"'
# A second request changes nothing.
kill -TERM "$server_pid"

# Bots arriving now are refused.
"$bots" --config "$work/late.conf" >"$work/late.log" 2>&1 || true

# The server is still serving the first bots; once they leave, it stops.
sleep 0.2
kill -0 "$server_pid"
kill -TERM "$playing_pid"
wait "$playing_pid" || true
server_exit=0
wait "$server_pid" || server_exit=$?
pids=()
cat "$work/server.log" "$work/playing.log" "$work/late.log"
[ "$server_exit" -eq 0 ]
grep -q '"reason":"drained"' "$work/server.log"
grep -q '"admissionsRefused":2' "$work/server.log"
grep -q '"bots":2,"admitted":0,"unavailable":2' "$work/late.log"
echo "drained in order"
