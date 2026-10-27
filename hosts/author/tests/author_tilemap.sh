#!/usr/bin/env bash
# A tilemap painted in a session, headless: an empty level written as
# a file, a floor of ground painted along its bottom row and a cell of it
# taken away again, written as the tilemap module writes it and undone back
# to the file's very bytes; a tile past the sheet and a rectangle past the
# grid refused with the file untouched; then imported from outside into a
# copy of Ledges as a tilemap and cooked.
#
# usage: author_tilemap.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
trap 'echo "author_tilemap: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
printf 'tilemap 1\ntileset 00000000000000b2 4 1\ncell 1\nsolid 0\ngrid 5 3\ntiles\n. . . . .\n. . . . .\n. . . . .\n' \
    >"$work/root/level.tilemap"
cp "$work/root/level.tilemap" "$work/original.tilemap"
game="$repository/games/ledges/ledges.game"
paint() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"tilemap.paint","column":'"$1"',"row":'"$2"',"width":'"$3"',"height":'"$4"',"tile":'"$5"'}]}'
}
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.tilemap","request":'"$(paint 0 2 5 1 0)"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"level.tilemap","request":'"$(paint 2 2 1 1 -1)"'}'
    echo '{"kind":"authoring.history","id":4,"scene":"level.tilemap"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"level.tilemap","request":'"$(paint 0 0 1 1 4)"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"level.tilemap","request":'"$(paint 3 0 3 1 1)"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 3 | grep -q '"written":true,.*"undoable":2,"redoable":0'
reply 4 | grep -q '"entries":\[{"summary":"tilemap.paint","deltas":[1-9][0-9]*,"applied":true,"kind":"change"},{"summary":"tilemap.paint","deltas":[1-9][0-9]*,"applied":true,"kind":"change"}\]'
# The bottom row ground but for its middle; the rows above as they were.
grep -qx '0 0 . 0 0' "$work/root/level.tilemap"
[ "$(grep -cx '\. \. \. \. \.' "$work/root/level.tilemap")" = 2 ]
for id in 5 6; do
    reply $id | grep -q '"code":"validation_failed",.*"message":"tiles painted are a rectangle of whole cells within the grid'
done
cp "$work/root/level.tilemap" "$work/painted.tilemap"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.tilemap","request":'"$(paint 0 0 5 3 1)"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"level.tilemap"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 3 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/level.tilemap" "$work/painted.tilemap"

# Imported from outside into a copy of Ledges, declared, and cooked.
cp -R "$repository/games/ledges" "$work/ledges"
# The last session's replies go first, so the wait below never finds the
# cook's reply among them (gate-58).
rm -f "$work/replies"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.import","id":2,"source":"'"$work/painted.tilemap"'","as":"levels/second.tilemap"}'
    echo '{"kind":"authoring.cook","id":3,"output":"'"$work/content"'","cache":null}'
    for _ in $(seq 2400); do
        grep -q '"id":3,"answer"\|"id":3,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.end","id":4}'
} | "$author" session "$work/ledges/ledges.game" --cook "$cook" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.imported","asset":"tilemap",.*"path":"levels/second.tilemap"'
grep -q '"importer": "rawframe.tilemap"' "$work/ledges/levels/second.tilemap.rfmeta"
grep -q '^tilemap [0-9a-f]\{16\} levels/second.tilemap$' "$work/ledges/ledges.game"
reply 3 | grep -q '"answer":{"kind":"authoring.cooked"'
grep -q 'levels/second.tilemap' "$work/content/cook.receipt"
echo "painted a tilemap in a session and cooked it"
