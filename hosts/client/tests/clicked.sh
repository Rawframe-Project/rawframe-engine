#!/usr/bin/env bash
# A dedicated server playing a game, and a client whose player joins it from
# a real window drawn on the one device, clicked as a user would at each
# point given once its screen is lit (D421): prints every log, the server's
# first. A machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set. Run under an X server whose root window is
# black (Xvfb -br), from the repository root.
#
# usage: clicked.sh <rawframe-server> <rawframe-client> <client settings>
#                   <game> <x>,<y> [<x>,<y>...]
set -uo pipefail

here="$(dirname "$0")"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 900 "$4" /dev/null "$3" >"$log" 2>&1 &
play=$!
python3 "$here/click.py" "$play" "${@:5}"
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
