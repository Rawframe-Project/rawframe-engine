#!/usr/bin/env bash
# Runs a dedicated server and bots processes against each other over QUIC on
# this machine, and prints every log, the server's first. The server makes a
# self-signed identity and writes its fingerprint; the bots pin it. Run from
# the repository root.
#
#   play.sh <rawframe-server> <rawframe-bots> <bots per process> [processes]
#           [bots iterations] [game] [server settings] [bots settings]
#
# The settings files, when given, are appended to each side's configuration.
#
# Iterations are the Host's, at 120 a second; the server ticks at 60. The
# server runs until every bots process has stopped and is then asked to stop,
# so a slow start (a sanitizer, a busy machine) cannot end play early.
set -euo pipefail

server="$1"
bots="$2"
count="$3"
processes="${4:-1}"
bots_iterations="${5:-240}"
game="${6:-games/arena/arena.game}"
server_settings="${7:-/dev/null}"
bots_settings="${8:-/dev/null}"
work="$(mktemp -d)"
pids=()
trap 'kill "${pids[@]}" 2>/dev/null || true; rm -rf "$work"' EXIT

# A port nothing holds right now.
port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $game
kest.library = third_party/kest/lib/
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
cat "$server_settings" >>"$work/server.conf"
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = $bots_iterations
host.iteration_rate = 120
kest.game = $game
kest.library = third_party/kest/lib/
kest.plan_only = true
network.quic.pin_file = $work/fingerprint
bots.count = $count
bots.endpoint = 127.0.0.1:$port
CONF
cat "$bots_settings" >>"$work/bots.conf"

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 100); do
    grep -q '"code":"listening"' "$work/server.log" 2>/dev/null && break
    sleep 0.05
done
for index in $(seq "$processes"); do
    "$bots" --config "$work/bots.conf" >"$work/bots-$index.log" 2>&1 &
    pids+=($!)
done
for pid in "${pids[@]}"; do
    wait "$pid" || true
done
pids=()
kill -TERM "$server_pid"
wait "$server_pid" || true
cat "$work/server.log" "$work"/bots-*.log
