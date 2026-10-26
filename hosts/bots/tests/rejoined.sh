#!/usr/bin/env bash
# A connection lost without the server's word is asked for again: a
# bot is admitted, then its server is frozen past the connection's idle time
# (a second here), as a phone's suspended client comes back to one gone;
# the bot says it lost its connection, the server is let go on, and the bot
# is admitted again, its World mirrored from nothing. Run from the
# repository root.
#
#   rejoined.sh <rawframe-server> <rawframe-bots>
set -euo pipefail

server="$1"
bots="$2"
here="$PWD"
work="$(mktemp -d)"
pids=()
trap 'kill -CONT ${pids[@]+"${pids[@]}"} 2>/dev/null || true; kill ${pids[@]+"${pids[@]}"} 2>/dev/null || true
      rm -rf "$work"' EXIT
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $here/games/arena/arena.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
network.quic.idle_timeout_ms = 1000
network.quic.keep_alive_ms = 300
replication.endpoint = 127.0.0.1:$port
CONF
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = 1200
host.iteration_rate = 120
kest.plan_only = true
kest.game = $here/games/arena/arena.game
network.quic.pin_file = $work/fingerprint
network.quic.idle_timeout_ms = 1000
network.quic.keep_alive_ms = 300
bots.count = 1
bots.endpoint = 127.0.0.1:$port
bots.retry_ms = 200
CONF

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
pids+=("$server_pid")
for _ in $(seq 200); do
    [ -s "$work/fingerprint" ] && break
    sleep 0.05
done
"$bots" --config "$work/bots.conf" >"$work/bots.log" 2>&1 &
bots_pid=$!
pids+=("$bots_pid")
waited() {
    for _ in $(seq 400); do
        grep -q "$1" "$work/bots.log" 2>/dev/null && return 0
        sleep 0.05
    done
    echo "rejoined: never saw $1"
    cat "$work/bots.log"
    exit 1
}
waited '"code":"bots_admitted"'
kill -STOP "$server_pid"
waited '"code":"connection_lost"'
kill -CONT "$server_pid"
wait "$bots_pid" || true
kill "$server_pid" 2>/dev/null || true
summary="$(grep -o '"code":"bots_summary".*' "$work/bots.log" || true)"
echo "rejoined: $(grep -o '"bots":[0-9]*,"admitted":[0-9]*' <<<"$summary"), $(grep -o '"retried":[0-9]*' <<<"$summary"), \
$(grep -o '"mirroredEntities":[0-9]*' <<<"$summary")"
