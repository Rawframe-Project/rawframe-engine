#!/usr/bin/env bash
# Studio ends promptly while its preview no longer answers (D947): a game's
# preview live, its client stopped by the system as a loaded machine stalls
# one, and Studio then asked to stop. The links to the preview send their
# last record and wait a moment, not ten seconds, for a reply that cannot
# come. Prints Studio's exit, how long its stop took, and how long until the
# process was gone: its children were let go after its Host stopped, and
# waited out every link's last record there (D968). A machine with no
# adapter the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is
# set. Run under an X server, from the repository root.
#
#   frozen_preview.sh <rawframe-studio> <rawframe-server> <rawframe-client> <game description>
set -uo pipefail

work="$(mktemp -d)"
client=""
trap '[ -n "$client" ] && kill -CONT "$client" 2>/dev/null; rm -rf "$work"' EXIT
cp -R "$(dirname "$4")" "$work/game"
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 36000
host.iteration_rate = 120
render.device = any
# No one answers a message box here: a missing device is a skip (D962).
render.unavailable_says = false
studio.game = $work/game/$(basename "$4")
studio.play.directory = $work/studio-play
studio.play.server = $2
studio.play.client = $3
studio.preferences = $work/studio.preferences
CONF
"$1" --config "$work/studio.conf" >"$work/log" 2>&1 &
studio=$!
for _ in $(seq 1200); do
    grep -q '"code":"studio_previewing"\|"device_unavailable"' "$work/log" && break
    kill -0 "$studio" 2>/dev/null || break
    sleep 0.1
done
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    kill -TERM "$studio" 2>/dev/null
    wait "$studio"
    echo "skip: no device"
    exit 0
fi
# Its client, started beside the session's preview, once it runs.
for _ in $(seq 300); do
    client="$(pgrep -P "$studio" -x rawframe-client | head -1)"
    [ -n "$client" ] && break
    sleep 0.1
done
[ -n "$client" ] && kill -STOP "$client"
echo "preview frozen: $([ -n "$client" ] && echo yes || echo no)"
asked=$(date +%s%N)
kill -TERM "$studio"
wait "$studio"
echo "gone in $((($(date +%s%N) - asked) / 1000000000)) s"
grep -o '"exit":"[a-z_]*","exitCode":[0-9]*,"shutdownMs":[0-9]*' "$work/log" | tail -1
