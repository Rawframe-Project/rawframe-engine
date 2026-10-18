#!/usr/bin/env bash
# A blockout authored in a session (D849), headless: a yard written as a
# file, its lines read, a ramp put in and a pit cut, kept as written; a
# solid its reader refuses, refused with the file untouched; a line taken
# away and undone back to its bytes; then the blockout imported from
# outside into a copy of Runners as a mesh with the blockout importer,
# declared by the game, and cooked (D848).
#
# usage: author_blockout.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
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
trap 'echo "author_blockout: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
printf 'blockout 1\n# the yard\nbox floor at 0 0 0 size 8 0.5 8\n' >"$work/root/yard.blockout"
cp "$work/root/yard.blockout" "$work/original.blockout"
game="$repository/games/runners/runners.game"
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
insert() {
    echo '{"operation":"lines.insert","line":'"$1"',"text":"'"$2"'"}'
}
read_lines='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"lines.read"}]}'
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"yard.blockout","queries":'"$read_lines"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"yard.blockout","request":'"$(request "$(insert 3 "ramp up at 3 0.5 0 size 2 1 3 turn 90"),$(insert 4 "cut box pit at -2 -0.5 -2 size 2 2 2")")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"yard.blockout","queries":'"$read_lines"'}'
    echo '{"kind":"authoring.apply","id":5,"scene":"yard.blockout","request":'"$(request "$(insert 3 "sphere ball at 0 0 0")")"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"answer":{"lines":\["blockout 1","# the yard","box floor at 0 0 0 size 8 0.5 8"\]'
reply 3 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 4 | grep -q '"box floor at 0 0 0 size 8 0.5 8","ramp up at 3 0.5 0 size 2 1 3 turn 90","cut box pit at -2 -0.5 -2 size 2 2 2"'
grep -qx 'cut box pit at -2 -0.5 -2 size 2 2 2' "$work/root/yard.blockout"
reply 5 | grep -q '"code":"validation_failed",.*"message":"the blockout does not read: blockout line 4: '
cp "$work/root/yard.blockout" "$work/changed.blockout"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"yard.blockout","request":'"$(request "$(insert 3 "sphere ball at 0 0 0")")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"yard.blockout","request":'"$(request '{"operation":"lines.set","line":3,"text":null}')"'}'
    echo '{"kind":"authoring.undo","id":4,"scene":"yard.blockout"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"code":"validation_failed"'
cmp "$work/root/yard.blockout" "$work/changed.blockout"
reply 3 | grep -q '"written":true'
reply 4 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/yard.blockout" "$work/changed.blockout"

# Imported from outside into a copy of Runners, declared, and cooked.
cp -R "$repository/games/runners" "$work/runners"
# The last session's replies go first, so the wait below never finds the
# cook's reply among them (gate-58).
rm -f "$work/replies"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.import","id":2,"source":"'"$work/changed.blockout"'","as":"blockouts/yard.blockout"}'
    echo '{"kind":"authoring.cook","id":3,"output":"'"$work/content"'","cache":null}'
    for _ in $(seq 2400); do
        grep -q '"id":3,"answer"\|"id":3,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.end","id":4}'
} | "$author" session "$work/runners/runners.game" --cook "$cook" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.imported","asset":"mesh",.*"path":"blockouts/yard.blockout"'
grep -q '"importer": "rawframe.blockout"' "$work/runners/blockouts/yard.blockout.rfmeta"
grep -q '^mesh [0-9a-f]\{16\} blockouts/yard.blockout$' "$work/runners/runners.game"
reply 3 | grep -q '"answer":{"kind":"authoring.cooked"'
grep -q 'blockouts/yard.blockout' "$work/content/cook.receipt"
echo "authored a blockout in a session and cooked it as a mesh"
