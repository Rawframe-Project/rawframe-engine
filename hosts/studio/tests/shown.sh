#!/usr/bin/env bash
# Studio opened on a game (D435): its window drawn on the one device for
# two seconds, its session welcomed, and its panels shown, a row for each
# scene beside the game. Prints Studio's log. A machine with no adapter the
# configuration allows skips, unless RAWFRAME_REQUIRE_GPU is set. Run under
# an X server, from the repository root.
#
#   shown.sh <rawframe-studio> <game description>
set -uo pipefail

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 240
host.iteration_rate = 120
render.device = any
studio.game = $PWD/$2
studio.play.directory = $work/studio-play
CONF
# Its window the screen's size: no window manager runs to maximize it (D925).
grep -q '^window.height' "$work/studio.conf" || printf 'window.height = 720\n' >>"$work/studio.conf"
# Its preferences its own, never those of whoever runs it (D925).
grep -q '^studio.preferences' "$work/studio.conf" ||
    printf 'studio.preferences = %s\n' "$work/studio.preferences" >>"$work/studio.conf"
"$1" --config "$work/studio.conf" >"$work/log" 2>&1
status=$?
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/log"
exit "$status"
