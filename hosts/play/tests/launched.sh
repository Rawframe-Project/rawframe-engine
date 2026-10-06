#!/usr/bin/env bash
# The launcher plays a game as a standalone export does (D395): it starts
# the dedicated server, waits for its fingerprint, starts the client pinned
# to it (here a bots process with one bot, which plays for eight seconds and
# ends), and then asks the server to stop. Prints the client's log, the
# server's, and the launcher's exit code. Run from the repository root.
#
#   launched.sh <rawframe-play> <rawframe-server> <rawframe-bots> <game>
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

# A port nothing holds right now.
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"

# Every path in the three files is under their directory.
cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $game
network.quic.self_signed = true
network.quic.fingerprint_file = fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
cat >"$work/client.conf" <<CONF
host.maximum_iterations = 960
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
# A fingerprint left from another run is removed before the server starts.
printf 'stale\n' >"$work/fingerprint"

status=0
"$play" --config "$work/play.conf" || status=$?
cat "$work/client.log" "$work/server.log"
echo "play: exit $status"
