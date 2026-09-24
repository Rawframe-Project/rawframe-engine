#!/usr/bin/env bash
# Runs a dedicated server and a bots process against each other over QUIC on
# this machine, and prints both logs. The server makes a self-signed identity
# and writes its fingerprint; the bots pin it. Run from the repository root.
#
#   play.sh <rawframe-server> <rawframe-bots> <bots> [server iterations] [bots iterations]
set -euo pipefail

server="$1"
bots="$2"
count="$3"
server_iterations="${4:-480}"
bots_iterations="${5:-240}"
work="$(mktemp -d)"
trap 'kill "${server_pid:-}" 2>/dev/null || true; rm -rf "$work"' EXIT

# A port nothing holds right now.
port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"

cat >"$work/server.conf" <<CONF
host.maximum_iterations = $server_iterations
host.iteration_rate = 120
world.tick_rate = 60
kest.game = games/arena/arena.game
kest.library = third_party/kest/lib/
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = $bots_iterations
host.iteration_rate = 120
kest.game = games/arena/arena.game
kest.library = third_party/kest/lib/
kest.plan_only = true
network.quic.pin_file = $work/fingerprint
bots.count = $count
bots.endpoint = 127.0.0.1:$port
CONF

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 100); do
    grep -q '"code":"listening"' "$work/server.log" 2>/dev/null && break
    sleep 0.05
done
"$bots" --config "$work/bots.conf" >"$work/bots.log" 2>&1 || true
wait "$server_pid" || true
server_pid=
cat "$work/server.log" "$work/bots.log"
