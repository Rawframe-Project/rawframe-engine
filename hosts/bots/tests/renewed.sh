#!/usr/bin/env bash
# A server's self-signed identity renewed while it serves (D419): renewed a
# second after it starts (`network.quic.renew_ms`), it writes the new
# fingerprint over the old. Bots pinned before play on through it; bots
# pinned to the new fingerprint are admitted, and bots still pinned to the
# old one are refused. Run from the repository root.
#
#   renewed.sh <rawframe-server> <rawframe-bots>
set -euo pipefail

server="$1"
bots="$2"
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
here="$(native "$PWD")"
work="$(native "$(mktemp -d)")"
pids=()
trap 'kill ${pids[@]+"${pids[@]}"} 2>/dev/null || true; rm -rf "$work"' EXIT
port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $here/games/arena/arena.game
network.quic.self_signed = true
network.quic.renew_ms = 1000
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
# Bots of `name`, pinned to `pin`, for `iterations` at 120 a second.
bots_conf() {
    cat >"$work/$1.conf" <<CONF
host.maximum_iterations = $3
host.iteration_rate = 120
kest.plan_only = true
kest.game = $here/games/arena/arena.game
network.quic.pin_file = $2
bots.count = 2
bots.endpoint = 127.0.0.1:$port
CONF
}

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 200); do
    [ -s "$work/fingerprint" ] && break
    sleep 0.05
done
cp "$work/fingerprint" "$work/first"
# Pinned before the renewal, and playing through it.
bots_conf before "$work/first" 600
"$bots" --config "$work/before.conf" >"$work/before.log" 2>&1 &
pids+=($!)
for _ in $(seq 200); do
    grep -q '"code":"identity_renewed"' "$work/server.log" 2>/dev/null && break
    sleep 0.05
done
if cmp -s "$work/fingerprint" "$work/first"; then
    echo "the fingerprint file still holds the first identity"
    exit 1
fi
cp "$work/fingerprint" "$work/second"
bots_conf after "$work/second" 240
bots_conf stale "$work/first" 240
"$bots" --config "$work/after.conf" >"$work/after.log" 2>&1 || true
"$bots" --config "$work/stale.conf" >"$work/stale.log" 2>&1 || true
for pid in ${pids[@]+"${pids[@]}"}; do
    wait "$pid" || true
done
pids=()
kill -TERM "$server_pid"
wait "$server_pid" || true
summary() {
    grep -o '"bots":2,"admitted":[0-9]*' "$work/$1.log" | head -1
}
echo "renewed: before $(summary before), after $(summary after), stale $(summary stale)," \
    "renewals $(grep -c '"code":"identity_renewed"' "$work/server.log")"
