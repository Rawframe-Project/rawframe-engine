#!/usr/bin/env bash
# A dedicated server playing runners, and a client whose player joins it
# from a real window drawn on the one device (D280), while the screen is
# watched: prints every log and how many pixels the screen lit at most. A
# machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set, as the check sets it where lavapipe is.
# Run under an X server whose root window is black (Xvfb -br), from the
# repository root.
#
# usage: drawn.sh <rawframe-server> <rawframe-client> <client settings>
set -uo pipefail

here="$(dirname "$0")"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 480 games/runners/runners.game /dev/null "$3" >"$log" 2>&1 &
play=$!
python3 "$here/screen.py" "$play"
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
