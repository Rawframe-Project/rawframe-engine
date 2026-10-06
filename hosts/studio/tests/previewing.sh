#!/usr/bin/env bash
# Studio previewing a scene in a running game (D433, D440): a dedicated
# server plays the game, and a client whose player joins it from a real
# window also serves a tooling endpoint with the view grant. Once the
# player is admitted, Studio opens on the game with that endpoint as its
# preview, and is clicked at the points given (tools/click.py): choosing a
# scene attaches the preview, and a view typed is handed to it. Studio
# stopping ends its session, which gives the player's camera back; then
# the client is stopped. Prints the clicks and every log, Studio's first.
# A machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set. Run under an X server whose root window is
# black (Xvfb -br), from the repository root.
#
# usage: previewing.sh <rawframe-server> <rawframe-client> <rawframe-studio>
#                      <client settings> <game> <server settings>
#                      <x>,<y>[:<text>]...
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/play"
port="$(python3 "$here/../../../tools/free_port.py")"
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/token"
cat "$4" >"$work/client.conf"
cat >>"$work/client.conf" <<CONF
network.quic.self_signed = true
network.quic.fingerprint_file = $work/preview.fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
tooling.grants = view
CONF
RAWFRAME_PLAY_WORK="$work/play" bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 36000 "$5" "$6" \
    "$work/client.conf" >"$work/log" 2>&1 &
play=$!
# Admitted, and the endpoint's identity written.
for _ in $(seq 1200); do
    if grep -q '"code":"bots_admitted"' "$work/play/bots-1.log" 2>/dev/null && [ -s "$work/preview.fingerprint" ]; then
        break
    fi
    kill -0 "$play" 2>/dev/null || break
    sleep 0.25
done
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
render.device = any
studio.game = $PWD/$5
studio.preview.endpoint = 127.0.0.1:$port
studio.preview.pin_file = $work/preview.fingerprint
studio.preview.token_file = $work/token
CONF
"$3" --config "$work/studio.conf" >"$work/studio.log" 2>&1 &
studio=$!
echo "$studio" >"$work/studio.pid"
python3 "$here/../../../tools/click.py" "$studio" "$work/studio.log" studio_shown "$work/studio.pid" "${@:7}"
wait "$studio"
status=$?
sleep 1
kill -TERM "$(cat "$work/play/bots-1.pid" 2>/dev/null)" 2>/dev/null
wait "$play"
if grep -q '"device_unavailable"' "$work/log" "$work/studio.log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/studio.log" "$work/log"
exit "$status"
