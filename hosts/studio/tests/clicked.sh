#!/usr/bin/env bash
# Studio clicked as an author would (D435): opened on a game, its second
# scene clicked, then that scene's first entity, through XTest
# (tools/click.py), once Studio says it is shown. The scene's entities and
# the entity's components come from the session's reads, and the entity is
# chosen in the session. Prints the clicks and Studio's log. A machine with
# no adapter the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is
# set. Run under an X server whose root window is black (Xvfb -br), from
# the repository root.
#
#   clicked.sh <rawframe-studio> <game description> <x>,<y>...
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
render.device = any
studio.game = $PWD/$2
CONF
"$1" --config "$work/studio.conf" >"$work/log" 2>&1 &
studio=$!
echo "$studio" >"$work/studio.pid"
python3 "$here/../../../tools/click.py" "$studio" "$work/log" studio_shown "$work/studio.pid" "${@:3}"
wait "$studio"
status=$?
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/log"
exit "$status"
