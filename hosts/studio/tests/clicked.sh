#!/usr/bin/env bash
# Studio clicked as an author would (D436, D437): opened on a copy of a
# game, so what it writes is the copy's, and clicked at the points given
# through XTest (tools/click.py) once it says it is shown; a point with
# text types it into the field there and presses Enter. Prints the clicks,
# whether the scene named last was written, and Studio's log. A machine with
# no adapter the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is
# set. Run under an X server whose root window is black (Xvfb -br), from
# the repository root.
#
#   clicked.sh <rawframe-studio> <game description> <scene> <x>,<y>[:<text>]...
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp -R "$(dirname "$2")" "$work/game"
# A test may change the copy's scene first: a sed expression in
# STUDIO_SCENE_EDIT, as a scene authored against an older layout (D452).
if [ -n "${STUDIO_SCENE_EDIT:-}" ]; then
    sed -i "$STUDIO_SCENE_EDIT" "$work/game/$3"
fi
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
render.device = any
studio.game = $work/game/$(basename "$2")
CONF
# And settings of its own, in STUDIO_SETTINGS, such as the editor it opens
# a diagnostic in (D453), which says how it was opened in STUDIO_EDITED.
if [ -n "${STUDIO_SETTINGS:-}" ]; then
    printf '%s\n' "$STUDIO_SETTINGS" >>"$work/studio.conf"
fi
export STUDIO_EDITED="$work/edited"
"$1" --config "$work/studio.conf" >"$work/log" 2>&1 &
studio=$!
echo "$studio" >"$work/studio.pid"
python3 "$here/../../../tools/click.py" "$studio" "$work/log" studio_shown "$work/studio.pid" "${@:4}"
wait "$studio"
status=$?
if cmp -s "$(dirname "$2")/$3" "$work/game/$3"; then
    echo "$3 written: no"
else
    echo "$3 written: yes"
    # The scene as written, where a test reads what an edit wrote (D458).
    if [ -n "${STUDIO_SHOW_WRITTEN:-}" ]; then
        cat "$work/game/$3"
    fi
fi
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/edited" 2>/dev/null
cat "$work/log"
exit "$status"
