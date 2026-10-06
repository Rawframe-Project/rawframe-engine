#!/usr/bin/env bash
# A client previewing a game for an authoring session (D432, D433): a
# dedicated server plays it, and a client whose player joins it from a real
# window also serves a tooling endpoint with the view grant. Once its
# player is admitted, a rawframe-author session on the game attaches that
# endpoint as the preview of a scene, looks at the scene from above and
# then from lower, and lets the preview go, which gives the player's camera
# back; then the client is stopped. Prints the session's replies and every
# log, the server's first. A machine with no adapter the
# configuration allows skips, unless RAWFRAME_REQUIRE_GPU is set. Run under
# an X server, from the repository root.
#
# usage: previewed.sh <rawframe-server> <rawframe-client> <rawframe-author>
#                     <client settings> <game> <scene> [server settings]
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
RAWFRAME_PLAY_WORK="$work/play" bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 36000 "$5" "${7:-/dev/null}" \
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
preview='{"endpoint":"127.0.0.1:'"$port"'","pinFile":"'"$work/preview.fingerprint"'","tokenFile":"'"$work/token"'"}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.preview","id":2,"scene":"'"$6"'","preview":'"$preview"'}'
    echo '{"kind":"authoring.view","id":3,"scene":"'"$6"'","view":{"eye":[0,30,0.5],"target":[0,0,0],"fieldOfView":60}}'
    sleep 1
    echo '{"kind":"authoring.view","id":4,"scene":"'"$6"'","view":{"eye":[8,4,8],"target":[0,0,0],"fieldOfView":50}}'
    sleep 1
    echo '{"kind":"authoring.preview","id":5,"scene":"'"$6"'","preview":null}'
} | "$3" session "$5" >"$work/replies" 2>&1 || true
cat "$work/replies"
sleep 1
kill -TERM "$(cat "$work/play/bots-1.pid" 2>/dev/null)" 2>/dev/null
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/log"
exit "$status"
