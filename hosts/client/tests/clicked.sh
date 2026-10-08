#!/usr/bin/env bash
# A dedicated server playing a game, and a client whose player joins it from
# a real window drawn on the one device, clicked as a user would at each
# point given once its player is admitted and its screen is lit (D421,
# D430), and stopped a few seconds after: prints every log, the server's
# first. A machine with no adapter the configuration allows skips, unless
# RAWFRAME_REQUIRE_GPU is set. Run under an X server whose root window is
# black (Xvfb -br), from the repository root.
#
# usage: clicked.sh <rawframe-server> <rawframe-client> <client settings>
#                   <game> <x>,<y> [<x>,<y>...]
set -uo pipefail

here="$(dirname "$0")"
log="$(mktemp)"
work="$(mktemp -d)"
trap 'rm -rf "$log" "$work"' EXIT
# The server plays the cooked content the client's settings name, as a
# game with meshes is played from its content (D512).
grep '^content\.root' "$3" >"$work/content.conf" || true
# The client's iterations only bound it: a loaded machine admits it late.
RAWFRAME_PLAY_WORK="$work" bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 36000 "$4" "$work/content.conf" "$3" \
    >"$log" 2>&1 &
play=$!
python3 "$here/../../../tools/click.py" "$play" "$work/bots-1.log" bots_admitted "$work/bots-1.pid" "${@:5}"
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
