#!/usr/bin/env bash
# A dedicated server playing a game (runners unless named), and a client
# whose player joins it from a real window drawn on the one device (D280),
# while the screen is watched: prints every log and how many pixels the screen lit at most. A
# machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set, as the check sets it where lavapipe is.
# Run under an X server whose root window is black (Xvfb -br), from the
# repository root.
#
# Its window is resized to 800 by 600 once the screen is lit.
#
# usage: drawn.sh <rawframe-server> <rawframe-client> <client settings>
#                 [game] [server settings] [bots] [iterations]
#
# [bots], none unless given, play beside the client's own player in its
# process: flat players beside one on a headset (D606). [iterations] are
# the client's Host's, 480 unless given.
set -uo pipefail

here="$(dirname "$0")"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
bash "$here/../../bots/tests/play.sh" "$1" "$2" "${6:-0}" 1 "${7:-480}" "${4:-games/runners/runners.game}" "${5:-/dev/null}" "$3" \
    >"$log" 2>&1 &
play=$!
python3 "$here/screen.py" "$play" "" 800x600
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
