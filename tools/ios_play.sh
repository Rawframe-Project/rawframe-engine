#!/usr/bin/env bash
# Plays a game in a booted iOS simulator (D586): the dedicated server built
# for the simulator runs there through simctl spawn, and the iOS client
# application, installed and launched, plays its own player against it over
# QUIC until its iterations end. A simulator's processes read this
# machine's files where they are, so both read the game from the source
# tree, and the client's configuration is written into its application's
# Documents directory. Prints both sides' records of note; passes when the
# client was admitted and showed frames.
#
#   tools/ios_play.sh <build tree> [<game>] [<client iterations>]
#
# RAWFRAME_IOS_DEVICE names the simulator, the booted one when unset.
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$1"
game="$PWD/${2:-games/runners/runners.game}"
iterations="${3:-600}"
device="${RAWFRAME_IOS_DEVICE:-booted}"
app="$tree/hosts/ios_client/rawframe-client.app"
server="$tree/hosts/dedicated_server/rawframe-server.app/rawframe-server"
id=dev.rawframe.client
work="$(mktemp -d)"
server_pid=""
trap '[ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
      xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
      rm -rf "$work"' EXIT

port="$(python3 tools/free_port.py)"
# A minute at most, the client's run well inside it.
cat >"$work/server.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
xcrun simctl spawn "$device" "$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 600); do
    [ -s "$work/fingerprint" ] && break
    sleep 0.1
done
[ -s "$work/fingerprint" ] || { echo "the server wrote no fingerprint"; cat "$work/server.log"; exit 1; }

xcrun simctl install "$device" "$app"
documents="$(xcrun simctl get_app_container "$device" "$id" data)/Documents"
mkdir -p "$documents"
rm -f "$documents/client.log"
cat >"$documents/client.conf" <<CONF
host.maximum_iterations = $iterations
host.iteration_rate = 120
kest.plan_only = true
kest.game = $game
network.quic.pin_file = $work/fingerprint
bots.count = 0
bots.player = true
bots.endpoint = 127.0.0.1:$port
input.gamepads = false
render.frame_rate = 30
CONF
xcrun simctl launch "$device" "$id" >/dev/null
# Three minutes at most for its run to end: the application ends its
# process once its Host has stopped.
for _ in $(seq 180); do
    grep -q '"code":"stopped"' "$documents/client.log" 2>/dev/null && break
    sleep 1
done

for side in server client; do
    log="$work/server.log"
    [ "$side" = client ] && log="$documents/client.log"
    echo "== $side"
    grep -o '"code":"\(bots_admitted\|bots_summary\|frame_summary\|device_ready\|device_unavailable\|start_failed\|stopped\)"[^}]*}[^}]*}' \
        "$log" 2>/dev/null || { echo "nothing of note:"; tail -20 "$log" 2>/dev/null || true; }
done
grep -q '"code":"bots_admitted"' "$documents/client.log" && grep -q '"framesShown":[1-9]' "$documents/client.log"
