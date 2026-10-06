#!/usr/bin/env bash
# Studio playing a game to preview it (D445): opened on a game with the
# dedicated server's and the client's programs to play it, and clicked at
# the points given (tools/click.py) once it says it is shown. Prints the
# clicks, Studio's log, and the played client's and server's logs. A
# machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set. Run under an X server whose root window is
# black (Xvfb -br), from the repository root.
#
# usage: plays.sh <rawframe-studio> <rawframe-server> <rawframe-client> <game>
#                 <server settings> <client settings>
#                 <x>,<y>[:<text>] | wait=<seconds> ...
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# The played client's window beside Studio's, clear of where Studio is
# clicked: with no window manager, a later window lies over an earlier.
cat "$6" >"$work/client.conf"
printf 'window.width = 560\nwindow.height = 360\nwindow.x = 700\nwindow.y = 340\n' >>"$work/client.conf"
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 36000
host.iteration_rate = 120
render.device = any
studio.game = $PWD/$4
studio.play.server = $2
studio.play.client = $3
studio.play.server_settings = $5
studio.play.client_settings = $work/client.conf
studio.play.directory = $work/play
CONF
"$1" --config "$work/studio.conf" >"$work/studio.log" 2>&1 &
studio=$!
echo "$studio" >"$work/studio.pid"
python3 "$here/../../../tools/click.py" "$studio" "$work/studio.log" studio_shown "$work/studio.pid" "${@:7}"
wait "$studio"
status=$?
if grep -q '"device_unavailable"' "$work/studio.log" "$work/play/client.log" 2>/dev/null && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/studio.log" "$work/play/client.log" "$work/play/server.log" 2>/dev/null
exit "$status"
