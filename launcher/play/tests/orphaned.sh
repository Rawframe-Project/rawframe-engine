#!/usr/bin/env bash
# A launcher killed leaves nothing playing (D649): the launcher starts the
# arena's dedicated server and a bots client set to play for a minute, both
# told to stop with it (D646), and is killed as a task manager kills it;
# both stop in order within ten seconds, on every system. Run from the
# repository root.
#
#   orphaned.sh <rawframe-play> <rawframe-server> <rawframe-bots> <game>
set -euo pipefail

play="$1"
server="$2"
bots="$3"
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
game="$(native "$PWD")/$4"
work="$(native "$(mktemp -d)")"
trap 'rm -rf "$work"' EXIT

port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
cat >"$work/server.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $game
network.quic.self_signed = true
network.quic.fingerprint_file = fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
cat >"$work/client.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
kest.game = $game
kest.plan_only = true
network.quic.pin_file = fingerprint
bots.count = 1
bots.endpoint = 127.0.0.1:$port
CONF
cat >"$work/play.conf" <<CONF
play.server = $(native "$server")
play.server_config = server.conf
play.server_log = server.log
play.client = $(native "$bots")
play.client_config = client.conf
play.client_log = client.log
play.fingerprint = fingerprint
CONF

"$play" --config "$work/play.conf" &
launcher=$!
# Until both play: the bot admitted.
for _ in $(seq 300); do
    grep -q '"code":"bots_admitted"' "$work/client.log" 2>/dev/null && break
    sleep 0.1
done
grep -q '"code":"bots_admitted"' "$work/client.log"
# Killed outright: Windows' own kill for a program Git's bash started.
if [ -r "/proc/$launcher/winpid" ]; then
    taskkill //F //PID "$(cat "/proc/$launcher/winpid")" >/dev/null
else
    kill -9 "$launcher"
fi
wait "$launcher" || true
stopped() { grep -q '"code":"stopped"' "$work/$1"; }
for _ in $(seq 100); do
    stopped server.log && stopped client.log && break
    sleep 0.1
done
if ! stopped server.log || ! stopped client.log; then
    cat "$work/client.log" "$work/server.log"
    if command -v pkill >/dev/null 2>&1; then
        pkill -9 -f "$work/server.conf" || true
        pkill -9 -f "$work/client.conf" || true
    fi
    echo "a program outlived the launcher"
    exit 1
fi
echo "both stopped with the launcher"
