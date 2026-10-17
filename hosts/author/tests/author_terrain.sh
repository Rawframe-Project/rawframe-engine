#!/usr/bin/env bash
# A terrain shaped in a session (D855), headless: a flat field written as
# a file, raised a meter at its middle by a brush's stroke and painted, the
# strokes written as the terrain module makes them and undone back to the
# file's very bytes; a tool no brush has and a layer the terrain does not
# name refused with the file untouched; then imported from outside into a
# copy of Ledges, small enough to cook in time on a loaded machine, as a
# mesh with the terrain importer and cooked (D854).
#
# usage: author_terrain.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
trap 'echo "author_terrain: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
{
    printf 'terrain 1\ngrid 5 5\ncell 1\nlayer grass 00000000000000a1\nlayer rock 00000000000000b2\nheights\n'
    for _ in 1 2 3 4 5; do echo '0 0 0 0 0'; done
} >"$work/root/field.terrain"
cp "$work/root/field.terrain" "$work/original.terrain"
game="$repository/games/runners/runners.game"
brush() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"terrain.brush","tool":"'"$1"'","x":2,"z":2,"radius":'"$2"',"amount":'"$3"'}]}'
}
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"field.terrain","request":'"$(brush raise 1.5 1)"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"field.terrain","request":'"$(brush paint 0.8 1)"'}'
    echo '{"kind":"authoring.history","id":4,"scene":"field.terrain"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"field.terrain","request":'"$(brush dig 1 1)"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"field.terrain","request":'"$(brush paint 1 5)"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 3 | grep -q '"written":true,.*"undoable":2,"redoable":0'
reply 4 | grep -q '"entries":\[{"summary":"terrain.brush","deltas":[1-9][0-9]*,"applied":true},{"summary":"terrain.brush","deltas":[1-9][0-9]*,"applied":true}\]'
# The middle sample a meter up, its row's neighbors between, the edges
# flat; the four cells around the middle painted rock.
grep -qx '0 0.012 0.309 0.012 0' "$work/root/field.terrain"
grep -qx '0 0.309 1 0.309 0' "$work/root/field.terrain"
grep -qx '0110' "$work/root/field.terrain"
reply 5 | grep -q '"code":"validation_failed",.*"message":"a brush'"'"'s tool is raise, lower, smooth, or paint"'
reply 6 | grep -q '"code":"validation_failed",.*"message":"a paint'"'"'s amount is the place of a layer the terrain names"'
cp "$work/root/field.terrain" "$work/shaped.terrain"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.undo","id":2,"scene":"field.terrain"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
# A new session holds no history: the file stays as it was shaped.
reply 2 | grep -q '"code":"target_not_found"'
cmp "$work/root/field.terrain" "$work/shaped.terrain"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"field.terrain","request":'"$(brush lower 1.5 1)"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"field.terrain"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 3 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/field.terrain" "$work/shaped.terrain"

# Imported from outside into a copy of Ledges, declared, and cooked.
cp -R "$repository/games/ledges" "$work/ledges"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.import","id":2,"source":"'"$work/shaped.terrain"'","as":"ground/field.terrain"}'
    echo '{"kind":"authoring.cook","id":3,"output":"'"$work/content"'","cache":null}'
    for _ in $(seq 2400); do
        grep -q '"id":3,"answer"\|"id":3,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.end","id":4}'
} | "$author" session "$work/ledges/ledges.game" --cook "$cook" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.imported","asset":"mesh",.*"path":"ground/field.terrain"'
grep -q '"importer": "rawframe.terrain"' "$work/ledges/ground/field.terrain.rfmeta"
grep -q '^mesh [0-9a-f]\{16\} ground/field.terrain$' "$work/ledges/ledges.game"
reply 3 | grep -q '"answer":{"kind":"authoring.cooked"'
grep -q 'ground/field.terrain' "$work/content/cook.receipt"
echo "shaped a terrain in a session and cooked it as a mesh"
