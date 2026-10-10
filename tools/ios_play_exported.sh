#!/usr/bin/env bash
# Exports a game for iOS and plays the application made (D587), all in a
# booted simulator: rawframe-export, built for the simulator, runs there
# through simctl spawn with the cook, the build tool, and the server beside
# it, and makes the application from the iOS client's bundle; the exported
# server runs there as its folder has it, and the application, installed and
# launched, plays from the game and configuration in its own bundle,
# reaching the server on the simulator's loopback. The application's run is
# bounded by a line added to its bundle's configuration, as a simulator
# asks no signature. Passes as tools/ios_play.sh does, but for predictions
# confirmed, which a game that predicts none (Stalls) has none of.
#
#   tools/ios_play_exported.sh <build tree> [<game directory>] [<client iterations>]
#
# RAWFRAME_IOS_DEVICE names the simulator, the booted one when unset.
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$(cd "$1" && pwd)"
game="$PWD/${2:-games/runners}"
iterations="${3:-1200}"
device="${RAWFRAME_IOS_DEVICE:-booted}"
name="$(basename "$game")"
id="local.$name"
program() { printf '%s/hosts/%s/rawframe-%s.app/rawframe-%s' "$tree" "$1" "$2" "$2"; }
work="$(mktemp -d)"
out="$work/exported"
server_pid=""
trap '[ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
      xcrun simctl terminate "$device" "$id" >/dev/null 2>&1 || true
      rm -rf "$work"' EXIT

port="$(python3 tools/free_port.py)"
xcrun simctl spawn "$device" "$(program export export)" "$game" "$out" --target ios --address 127.0.0.1 \
    --port "$port" --cook "$(program cook cook)" --build "$(program build build)" \
    --server "$(program dedicated_server server)" --ios-client "$tree/hosts/ios_client/rawframe-client.app" \
    >"$work/export.log" 2>&1 || { echo "the export failed"; cat "$work/export.log"; exit 1; }
tail -1 "$work/export.log"

# Three minutes at most, as tools/ios_play.sh bounds its server.
printf 'host.maximum_iterations = 21600\n' >>"$out/server/server.conf"
xcrun simctl spawn "$device" "$out/server/rawframe-server" --config "$out/server/server.conf" \
    >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 600); do
    grep -q '"code":"listening"' "$work/server.log" 2>/dev/null && break
    sleep 0.1
done

application="$out/ios/$name.app"
printf 'host.maximum_iterations = %s\n' "$iterations" >>"$application/game/client.conf"
xcrun simctl install "$device" "$application"
documents="$(xcrun simctl get_app_container "$device" "$id" data)/Documents"
mkdir -p "$documents"
rm -f "$documents/client.log" "$documents/client.conf"
xcrun simctl launch "$device" "$id" >/dev/null
for _ in $(seq 180); do
    grep -q '"code":"stopped"' "$documents/client.log" 2>/dev/null && break
    sleep 1
done

# The server asked to stop as a supervisor asks, for its totals: the
# simulator's processes are this machine's.
pkill -TERM -f "$out/server/rawframe-server" || true
for _ in $(seq 200); do
    grep -q '"code":"stopped"' "$work/server.log" 2>/dev/null && break
    sleep 0.1
done

log="$documents/client.log"
grep -o '"code":"server_summary"[^}]*}[^}]*}' "$work/server.log" 2>/dev/null || true
grep -o '"code":"\(bots_admitted\|bots_summary\|surface_made\|device_ready\|device_unavailable\|accessibility_ready\|input_summary\|ui_summary\|start_failed\|stopped\)"[^}]*}[^}]*}' \
    "$log" 2>/dev/null || { echo "nothing of note:"; tail -20 "$log" 2>/dev/null || true; }
if grep -q '"code":"surface_made"' "$log" && grep -q '"code":"bots_admitted"' "$log" &&
    grep -q '"admitted":1,.*"stalled":0,' "$log" &&
    { grep -q '"framesShown":[1-9]' "$log" || grep -q '"reason":"no adapter answered"' "$log"; }; then
    exit 0
fi
echo "== the client's records"
grep -v '"kind":"metric"' "$log" | cut -c1-600 | head -80
echo "== the server's"
grep -v '"kind":"metric"' "$work/server.log" | cut -c1-400 | tail -20
exit 1
