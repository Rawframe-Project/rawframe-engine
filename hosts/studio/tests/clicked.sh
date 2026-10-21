#!/usr/bin/env bash
# Studio clicked as an author would (D436, D437): opened on a copy of a
# game, so what it writes is the copy's, and clicked at the points given
# through XTest (tools/click.py) once it says it is shown; a point with
# text types it into the field there and presses Enter. A point may be a
# control's name, `@Undo` (D774): Studio is read over a private
# accessibility bus this script starts, where dbus-daemon exists. Prints the clicks,
# whether the scene named last was written, and Studio's log. A machine with
# no adapter the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is
# set. Run under an X server whose root window is black (Xvfb -br), from
# the repository root.
#
#   clicked.sh <rawframe-studio> <game description> <scene> <x>,<y>[:<text>] | @<name>[:<text>]...
#
# With `none` for the game and the scene, Studio starts on its start screen
# with no game (D781), new games going to the test's own directory and the
# engine's templates offered.
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
source "$here/bus.sh"
trap '[ -n "$bus" ] && kill "$bus" 2>/dev/null; rm -rf "$work"' EXIT
if [ "$2" != none ]; then
    cp -R "$(dirname "$2")" "$work/game"
fi
# Or put a scene of its own in its place: a file in STUDIO_SCENE_FILE,
# as one with entities on layers (D788).
if [ -n "${STUDIO_SCENE_FILE:-}" ] && [ "$2" != none ]; then
    cp "$STUDIO_SCENE_FILE" "$work/game/$3"
fi
# And files of its own beside the game's: `from=to` pairs in
# STUDIO_EXTRA_FILES, `to` under the copy, as a capture to open (D843).
if [ -n "${STUDIO_EXTRA_FILES:-}" ] && [ "$2" != none ]; then
    for pair in $STUDIO_EXTRA_FILES; do
        mkdir -p "$work/game/$(dirname "${pair#*=}")"
        cp "${pair%%=*}" "$work/game/${pair#*=}"
    done
fi
# A test may change the copy's scene first: a sed expression in
# STUDIO_SCENE_EDIT, as a scene authored against an older layout (D452).
if [ -n "${STUDIO_SCENE_EDIT:-}" ] && [ "$2" != none ]; then
    sed -i "$STUDIO_SCENE_EDIT" "$work/game/$3"
fi
# Or put the copy under version control first, in STUDIO_GIT, its files
# one commit, as a game kept in git (D871); its log and status are told
# after Studio ends.
if [ -n "${STUDIO_GIT:-}" ] && [ "$2" != none ]; then
    git -C "$work/game" init -q
    git -C "$work/game" config user.name "Studio test"
    git -C "$work/game" config user.email "studio@test.invalid"
    git -C "$work/game" add -A
    git -C "$work/game" commit -q -m "the game as it was"
fi
# And tools beside one another, as an export puts them: `name=path` pairs
# in STUDIO_TOOLS, each linked under the test's `tools/` (D872).
if [ -n "${STUDIO_TOOLS:-}" ]; then
    mkdir -p "$work/tools"
    for pair in $STUDIO_TOOLS; do
        ln -s "${pair#*=}" "$work/tools/${pair%%=*}"
    done
fi
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 7200
host.iteration_rate = 120
render.device = any
studio.play.directory = $work/studio-play
CONF
if [ "$2" = none ]; then
    printf 'studio.templates = %s\nstudio.projects = %s\n' "$PWD/templates" "$work/games" >>"$work/studio.conf"
else
    printf 'studio.game = %s\n' "$work/game/$(basename "$2")" >>"$work/studio.conf"
fi
# And settings of its own, in STUDIO_SETTINGS, such as the editor it opens
# a diagnostic in (D453), which says how it was opened in STUDIO_EDITED;
# {work} in them is the test's own directory, where the preferences it
# keeps go (D779).
if [ -n "${STUDIO_SETTINGS:-}" ]; then
    printf '%s\n' "${STUDIO_SETTINGS//\{work\}/$work}" >>"$work/studio.conf"
fi
# Preferences to start from, in STUDIO_PREFERENCES, copied so Studio keeps
# its own in the test's directory (D781).
if [ -n "${STUDIO_PREFERENCES:-}" ]; then
    cp "$STUDIO_PREFERENCES" "$work/studio.preferences"
    printf 'studio.preferences = %s\n' "$work/studio.preferences" >>"$work/studio.conf"
fi
export STUDIO_EDITED="$work/edited"
# Its window the screen's size: no window manager runs to maximize it (D925).
grep -q '^window.height' "$work/studio.conf" || printf 'window.height = 720\n' >>"$work/studio.conf"
# Its preferences its own, never those of whoever runs it (D925).
grep -q '^studio.preferences' "$work/studio.conf" ||
    printf 'studio.preferences = %s\n' "$work/studio.preferences" >>"$work/studio.conf"
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
# And another scene the clicks write too, in STUDIO_ALSO_WRITTEN, as a move
# from one scene to another (D498).
if [ -n "${STUDIO_ALSO_WRITTEN:-}" ]; then
    if cmp -s "$(dirname "$2")/$STUDIO_ALSO_WRITTEN" "$work/game/$STUDIO_ALSO_WRITTEN"; then
        echo "$STUDIO_ALSO_WRITTEN written: no"
    else
        echo "$STUDIO_ALSO_WRITTEN written: yes"
    fi
fi
if [ -n "${STUDIO_GIT:-}" ] && [ "$2" != none ]; then
    git -C "$work/game" log --format='commit: %s'
    git -C "$work/game" status --porcelain | sed 's/^/left: /'
fi
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$work/edited" 2>/dev/null
cat "$work/studio.preferences" 2>/dev/null
cat "$work/log"
exit "$status"
